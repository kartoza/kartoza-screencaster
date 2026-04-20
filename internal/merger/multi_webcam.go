package merger

import (
	"fmt"
	"strings"

	"github.com/kartoza/kartoza-screencaster/internal/webcam"
)

const (
	bubbleGap = 20 // Gap between stacked bubbles in pixels
)

// webcamOverlayMulti holds parameters for a single bubble in a multi-bubble overlay
type webcamOverlayMulti struct {
	inputIdx   int
	side       webcam.WebcamSide
	size       int
	margin     int
	stackIndex int // 0 = bottom, 1 = above bottom, etc.
}

// webcamRectInput holds parameters for a single rectangle webcam in the strip
type webcamRectInput struct {
	inputIdx int
	device   string
}

// buildMultiBubbleFilter builds FFmpeg filter fragments for multiple circular webcam bubbles.
// Bubbles on the same side stack vertically from the bottom.
func buildMultiBubbleFilter(bubbles []webcamOverlayMulti, currentOutput string) string {
	var fragments []string

	for i, b := range bubbles {
		radius := b.size / 2
		outLabel := fmt.Sprintf("[out_bubble_%d]", i)

		// Calculate Y offset: bottom + (size + gap) * stackIndex
		yOffset := b.margin + (b.size+bubbleGap)*b.stackIndex

		// X position depends on side
		var xPos string
		if b.side == webcam.SideLeft {
			xPos = fmt.Sprintf("%d", b.margin)
		} else {
			xPos = fmt.Sprintf("W-w-%d", b.margin)
		}

		yPos := fmt.Sprintf("H-h-%d", yOffset)

		fragment := fmt.Sprintf(
			"[%d:v]scale='if(gt(iw,ih),-1,%d)':'if(gt(iw,ih),%d,-1)',crop=%d:%d,format=yuva420p,"+
				"geq=lum='p(X,Y)':cb='p(X,Y)':cr='p(X,Y)':"+
				"a='if(gt((X-%d)*(X-%d)+(Y-%d)*(Y-%d),%d*%d),0,255)'[bubble_%d_circle];"+
				"%s[bubble_%d_circle]overlay=%s:%s%s",
			b.inputIdx, b.size, b.size, b.size, b.size,
			radius, radius, radius, radius, radius, radius, i,
			currentOutput, i, xPos, yPos, outLabel,
		)

		fragments = append(fragments, fragment)
		currentOutput = outLabel
	}

	return strings.Join(fragments, ";")
}

// buildRectangleStripFilter builds an FFmpeg filter that creates a horizontal strip
// of equally-sized webcam feeds and stacks it below the main video.
func buildRectangleStripFilter(rects []webcamRectInput, videoWidth int, currentOutput string) string {
	if len(rects) == 0 {
		return ""
	}

	var fragments []string
	rectWidth := videoWidth / len(rects)

	// Scale each rectangle webcam to equal width
	var scaledLabels []string
	for i, r := range rects {
		label := fmt.Sprintf("[rect_%d]", i)
		fragment := fmt.Sprintf("[%d:v]scale=%d:-1:flags=lanczos%s", r.inputIdx, rectWidth, label)
		fragments = append(fragments, fragment)
		scaledLabels = append(scaledLabels, label)
	}

	// Combine into strip
	var stripLabel string
	if len(rects) == 1 {
		stripLabel = scaledLabels[0]
	} else {
		stripLabel = "[rect_strip]"
		hstack := fmt.Sprintf("%shstack=inputs=%d%s", strings.Join(scaledLabels, ""), len(rects), stripLabel)
		fragments = append(fragments, hstack)
	}

	// Stack strip below main video
	outLabel := "[out_rect_strip]"
	vstack := fmt.Sprintf("%s%svstack=inputs=2%s", currentOutput, stripLabel, outLabel)
	fragments = append(fragments, vstack)

	return strings.Join(fragments, ";")
}

// buildVerticalBottomThirdFilter builds an FFmpeg filter for the bottom-third section
// in vertical video mode, combining multiple webcam feeds side by side.
func buildVerticalBottomThirdFilter(rects []webcamRectInput, sectionWidth int, currentOutput string) string {
	if len(rects) == 0 {
		return ""
	}

	var fragments []string
	rectWidth := sectionWidth / len(rects)

	// Scale each webcam to equal width
	var scaledLabels []string
	for i, r := range rects {
		label := fmt.Sprintf("[bt_%d]", i)
		fragment := fmt.Sprintf("[%d:v]scale=%d:-1:flags=lanczos%s", r.inputIdx, rectWidth, label)
		fragments = append(fragments, fragment)
		scaledLabels = append(scaledLabels, label)
	}

	// Combine into strip if multiple
	var stripLabel string
	if len(rects) == 1 {
		stripLabel = scaledLabels[0]
	} else {
		stripLabel = "[bt_strip]"
		hstack := fmt.Sprintf("%shstack=inputs=%d%s", strings.Join(scaledLabels, ""), len(rects), stripLabel)
		fragments = append(fragments, hstack)
	}

	// Overlay the strip at the appropriate position (middle section)
	outLabel := "[out_bt]"
	overlay := fmt.Sprintf("%s%soverlay=(W-w)/2:(H*1/3)%s", currentOutput, stripLabel, outLabel)
	fragments = append(fragments, overlay)

	return strings.Join(fragments, ";")
}
