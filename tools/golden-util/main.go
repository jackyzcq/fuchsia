// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bufio"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"os"
	"path"
	"path/filepath"
	"sort"
	"strings"

	"github.com/google/go-cmp/cmp"
)

type flagsDef struct {
	manifest *string
	regen    *bool
}

var flags = flagsDef{
	manifest: flag.String("manifest", "", "JSON manifest"),
	regen:    flag.Bool("regen", false, "regen instead of testing"),
}

func printUsage() {
	program := path.Base(os.Args[0])
	message := `Usage: ` + program + ` [flags]

Utility used to test/regen golden files.

Flags:
`
	fmt.Fprint(flag.CommandLine.Output(), message)
	flag.PrintDefaults()
}

func main() {
	flag.Usage = printUsage
	flag.Parse()
	// We use log for errors, so clear flags to remove date/time.
	log.SetFlags(0)

	if *flags.manifest == "" {
		log.Fatal("must provide --manifest flag")
	}
	manifestJSON, err := ioutil.ReadFile(*flags.manifest)
	if err != nil {
		log.Fatal(err)
	}
	var manifest manifest
	if err := json.Unmarshal(manifestJSON, &manifest); err != nil {
		log.Fatalf("%s: %s", *flags.manifest, err)
	}
	if err := manifest.validate(); err != nil {
		log.Fatalf("%s: %s", *flags.manifest, err)
	}

	if *flags.regen {
		if err := manifest.regen(); err != nil {
			log.Fatal(err)
		}
	} else {
		passed, err := manifest.test()
		if err != nil {
			log.Fatal(err)
		}
		if !passed {
			fmt.Println("Run the test again with the --regen flag to regenerate goldens")
			os.Exit(1)
		}
	}
}

// A manifest stores the information needed to test/regen goldens.
type manifest struct {
	// Goldens directory used in test mode.
	TestGoldensDir string `json:"test_goldens_dir"`
	// Goldens directory used in regen mode.
	RegenGoldensDir string `json:"regen_goldens_dir"`
	// List of files to test/regen.
	Entries []entry `json:"entries"`
}

// An entry represents a file that gets compared to (in test mode) or
// overwritten by (in regen mode) another file generated by the build.
type entry struct {
	// Path to a golden file, relative to TestGoldensDir and RegenGoldensDir.
	Golden string `json:"golden"`
	// Path to the corresponding generated file.
	Generated string `json:"generated"`
}

func (m *manifest) validate() error {
	seen := make(map[string]struct{}, len(m.Entries))
	for _, entry := range m.Entries {
		if _, ok := seen[entry.Golden]; ok {
			return fmt.Errorf("%s: duplicate golden in manifest", entry.Golden)
		}
		seen[entry.Golden] = struct{}{}
		if filepath.Ext(entry.Golden) != ".golden" {
			return fmt.Errorf("%s: expected .golden extension", entry.Golden)
		}
		if filepath.Ext(entry.Generated) == ".golden" {
			return fmt.Errorf("%s: unexpected .golden extension", entry.Generated)
		}
	}
	return nil
}

func (m *manifest) regen() error {
	// Print the destination directory. Use an absolute path, since the provided
	// path is relative to GN's root_build_dir.
	absGoldensDir, err := filepath.Abs(m.RegenGoldensDir)
	if err != nil {
		return err
	}
	fmt.Printf("Regenerating goldens in %s\n", absGoldensDir)

	// Read the current contents of goldens.txt before overwriting it. If any
	// files it lists are no longer generated, we remove them at the end.
	goldensTxtPath := filepath.Join(m.RegenGoldensDir, "goldens.txt")
	goldensTxtFile, err := os.OpenFile(goldensTxtPath, os.O_RDWR|os.O_APPEND, 0)
	if err != nil {
		return err
	}
	defer goldensTxtFile.Close()
	goldensToRemove, err := readGoldensTxt(goldensTxtFile)
	if err != nil {
		return fmt.Errorf("%s: %s", goldensTxtPath, err)
	}
	// Write a newline in case the file doesn't end with one.
	goldensTxtFile.WriteString("\n")

	var allGoldenPaths []string
	for _, entry := range m.Entries {
		fmt.Printf("Writing %s\n", entry.Golden)
		goldenPath := filepath.Join(m.RegenGoldensDir, entry.Golden)
		if filepath.Dir(entry.Golden) != "." {
			os.MkdirAll(filepath.Dir(goldenPath), os.ModePerm)
		}
		goldenFile, err := os.Create(goldenPath)
		if err != nil {
			return err
		}
		defer goldenFile.Close()
		generatedPath := entry.Generated
		generatedFile, err := os.Open(generatedPath)
		if err != nil {
			return err
		}
		defer generatedFile.Close()
		if _, err := io.Copy(goldenFile, generatedFile); err != nil {
			return fmt.Errorf("copying %s to %s: %s", generatedPath, goldenPath, err)
		}
		allGoldenPaths = append(allGoldenPaths, entry.Golden)
		delete(goldensToRemove, entry.Golden)
		// Append to goldens.txt, even though we rewrite it at the end, so that
		// it remains accurate if something fails partway through.
		goldensTxtFile.WriteString(entry.Golden)
		goldensTxtFile.WriteString("\n")
	}

	// Purge stale goldens and empty parent directories.
	for path := range goldensToRemove {
		fmt.Printf("Removing %s\n", path)
		for ; path != "."; path = filepath.Dir(path) {
			if err := os.Remove(filepath.Join(m.RegenGoldensDir, path)); err != nil {
				break
			}
		}
	}

	// Rewrite goldens.txt with the new paths, sorted.
	sort.Strings(allGoldenPaths)
	goldensTxtFile.Truncate(0)
	goldensTxtFile.Seek(0, 0)
	for _, path := range allGoldenPaths {
		goldensTxtFile.WriteString(path)
		goldensTxtFile.WriteString("\n")
	}

	return nil
}

func (m *manifest) test() (bool, error) {
	// Read goldens.txt to ensure we only consider fresh host_test_data copies,
	// not old files that happen to remain in the build directory.
	goldensTxtPath := filepath.Join(m.TestGoldensDir, "goldens.txt")
	goldensTxtFile, err := os.Open(goldensTxtPath)
	if err != nil {
		return false, err
	}
	listedGoldens, err := readGoldensTxt(goldensTxtFile)
	goldensTxtFile.Close()
	if err != nil {
		return false, fmt.Errorf("%s: %s", goldensTxtPath, err)
	}

	var ts testSuite
	for _, entry := range m.Entries {
		tc := ts.testCase(entry.Golden)
		tc.announce()
		if _, ok := listedGoldens[entry.Golden]; !ok {
			tc.fail("file missing from goldens.txt (forgot to regen?)")
			continue
		}
		goldenPath := filepath.Join(m.TestGoldensDir, entry.Golden)
		goldenBytes, err := ioutil.ReadFile(goldenPath)
		if err != nil {
			tc.fail("%s", err)
			continue
		}
		generatedPath := entry.Generated
		generatedBytes, err := ioutil.ReadFile(generatedPath)
		if err != nil {
			tc.fail("%s", err)
			continue
		}
		if len(goldenBytes) != 0 && len(generatedBytes) == 0 {
			tc.fail("%s: generated file was unexpectedly empty", generatedPath)
			continue
		}
		goldenLines := strings.Split(string(goldenBytes), "\n")
		generatedLines := strings.Split(string(generatedBytes), "\n")
		if diff := cmp.Diff(goldenLines, generatedLines); diff != "" {
			tc.fail(`unexpected difference between golden file:
	%s
and generated file:
	%s
diff -golden +generated:
%s`,
				goldenPath, generatedPath, diff)
			continue
		}
		tc.pass()
	}
	ts.summarize()
	return !ts.failed, nil
}

func readGoldensTxt(rd io.Reader) (map[string]struct{}, error) {
	scanner := bufio.NewScanner(rd)
	paths := make(map[string]struct{})
	for scanner.Scan() {
		path := scanner.Text()
		// Omit empty lines to avoid spurious "" paths when setting up tests for
		// the first time (e.g. `touch goldens.txt` or `echo > goldens.txt`).
		if path != "" {
			paths[path] = struct{}{}
		}
	}
	if err := scanner.Err(); err != nil {
		return nil, err
	}
	return paths, nil
}

type testSuite struct {
	failed bool
}

func (s *testSuite) testCase(name string) testCase {
	return testCase{name, s}
}

func (s *testSuite) summarize() {
	if s.failed {
		fmt.Println("FAIL")
	} else {
		fmt.Println("PASS")
	}
}

type testCase struct {
	name  string
	suite *testSuite
}

func (c *testCase) announce() {
	fmt.Printf("=== TEST: %s\n", c.name)
}

func (c *testCase) pass() {
	fmt.Printf("--- PASS: %s\n", c.name)
}

func (c *testCase) fail(format string, arg ...interface{}) {
	fmt.Printf("--- FAIL: %s\n", c.name)
	fmt.Printf(format+"\n", arg...)
	c.suite.failed = true
}
