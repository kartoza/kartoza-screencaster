package main

import "github.com/kartoza/kartoza-screencaster/cmd"

// Version is set via ldflags during build
// Default shows next development version (last release + 1)
var version = "0.7.6"

func main() {
	cmd.SetVersion(version)
	cmd.Execute()
}
