/**
 * @file merger.h
 * @brief Post-recording video merging and processing utilities.
 *
 * Provides functions to combine screen, audio, and webcam recordings into
 * final output videos with logo overlays, loudness normalisation, and
 * support for both landscape and vertical (9:16) layouts.
 */
#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include "recorder/recorder.h" // for RecordingOptions

/** @brief Namespace encapsulating all post-recording merge operations. */
namespace Merger {

// ── Shared helpers ──────────────────────────────────────────────────────

/** @brief Convert a human-readable title to a filesystem-safe slug. */
QString titleSlug(const QString &title);

/**
 * @brief Build a standardised output filename.
 * @param number  Episode or recording number.
 * @param title   Human-readable title (will be slugified).
 * @param suffix  Optional file extension / suffix (e.g. ".mp4").
 * @return The composed filename string.
 */
QString outputFileName(int number, const QString &title, const QString &suffix = {});

/**
 * @brief Append FFmpeg input arguments for a logo overlay.
 * @param[in,out] args  Argument list to append to.
 * @param logo          Logo options describing the image/GIF source.
 */
void appendLogoInputArgs(QStringList &args, const RecordingOptions::LogoOpts &logo);

// ── FFmpeg utilities ────────────────────────────────────────────────────

/**
 * @brief Probe a video file and return its duration in microseconds.
 * @param filePath  Path to the video file.
 * @return Duration in microseconds, or 0 on failure.
 */
qint64 getVideoDurationUs(const QString &filePath);

/** @brief Simple width/height pair returned by getVideoDimensions(). */
struct VideoDimensions {
    int width = 0;  /**< Video width in pixels. */
    int height = 0; /**< Video height in pixels. */
};

/**
 * @brief Probe a video file and return its pixel dimensions.
 * @param filePath  Path to the video file.
 * @return A VideoDimensions struct (zero on failure).
 */
VideoDimensions getVideoDimensions(const QString &filePath);

/**
 * @brief Concatenate multiple video parts into a single file using FFmpeg concat demuxer.
 * @param parts       Ordered list of input file paths.
 * @param outputFile  Destination file path.
 * @param listPrefix  Prefix used for the temporary concat list file.
 * @return The output file path on success, or an empty string on failure.
 */
QString concatenateParts(const QStringList &parts, const QString &outputFile,
                         const QString &listPrefix = "concat");

// ── Audio processing ────────────────────────────────────────────────────

/** @brief Parameters extracted from an FFmpeg loudnorm first-pass analysis. */
struct LoudnormParams {
    bool valid = false;       /**< True if the analysis succeeded. */
    QString filterString;     /**< The loudnorm filter string for the second pass. */
};

/**
 * @brief Run a first-pass loudnorm analysis on an audio file.
 * @param audioFile  Path to the audio file to analyse.
 * @return Populated LoudnormParams (valid==false on error).
 */
LoudnormParams analyzeAudio(const QString &audioFile);

/**
 * @brief Apply two-pass loudness normalisation to an audio file.
 * @param inputFile   Source audio path.
 * @param outputFile  Destination audio path.
 * @param params      Loudnorm parameters from analyzeAudio().
 * @return True on success.
 */
bool normalizeAudio(const QString &inputFile, const QString &outputFile,
                    const LoudnormParams &params);

// ── FFmpeg progress runner ──────────────────────────────────────────────

/** @brief Callback type invoked with an integer percentage (0-100). */
using ProgressCallback = std::function<void(int percent)>;

/**
 * @brief Execute an FFmpeg command while reporting progress.
 * @param args        FFmpeg argument list (without the leading "ffmpeg").
 * @param durationUs  Expected output duration in microseconds (used for percentage).
 * @param onProgress  Optional callback invoked as progress updates arrive.
 * @return The FFmpeg process exit code.
 */
int runFFmpegWithProgress(const QStringList &args, qint64 durationUs,
                          const ProgressCallback &onProgress = nullptr);

// ── Filter building ─────────────────────────────────────────────────────

/** @brief Aggregated input paths and options needed by the merge filter builders. */
struct MergeInputs {
    QString screenFile;       /**< Path to the screen capture file. */
    QString audioFile;        /**< Path to the audio capture file. */
    QString webcamFile;       /**< Path to the webcam capture file. */
    RecordingOptions opts;    /**< Recording options (logos, layout, etc.). */
};

/**
 * @brief Build FFmpeg arguments for a merged landscape video.
 * @param in          Input files and recording options.
 * @param outputFile  Destination file path.
 * @return Complete FFmpeg argument list.
 */
QStringList buildMergedArgs(const MergeInputs &in, const QString &outputFile);

/**
 * @brief Build FFmpeg arguments for a vertical (9:16) video.
 * @param in          Input files and recording options.
 * @param outputFile  Destination file path.
 * @return Complete FFmpeg argument list.
 */
QStringList buildVerticalArgs(const MergeInputs &in, const QString &outputFile);

} // namespace Merger
