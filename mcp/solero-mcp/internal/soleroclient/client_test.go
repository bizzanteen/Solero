package soleroclient

import (
	"bufio"
	"encoding/json"
	"fmt"
	"net"
	"path/filepath"
	"testing"
)

// fakeSolero stands in for the running app: it reads one newline-framed request
// and writes back the canned response, newline-framed, in small chunks - which
// is what a real socket does with a large payload.
func fakeSolero(t *testing.T, resp map[string]any) string {
	t.Helper()
	path := filepath.Join(t.TempDir(), "ipc")
	ln, err := net.Listen("unix", path)
	if err != nil {
		t.Fatalf("listen: %v", err)
	}
	t.Cleanup(func() { ln.Close() })

	body, err := json.Marshal(resp)
	if err != nil {
		t.Fatalf("marshal: %v", err)
	}
	body = append(body, '\n')

	go func() {
		conn, err := ln.Accept()
		if err != nil {
			return
		}
		defer conn.Close()
		if _, err := bufio.NewReader(conn).ReadBytes('\n'); err != nil {
			return
		}
		for off := 0; off < len(body); off += 4096 {
			end := min(off+4096, len(body))
			if _, err := conn.Write(body[off:end]); err != nil {
				return
			}
		}
	}()
	return path
}

func modsResponse(n int) map[string]any {
	mods := make([]any, 0, n)
	for i := range n {
		mods = append(mods, map[string]any{
			"id":      fmt.Sprintf("mod-%04d-0123456789abcdef0123456789abcdef", i),
			"name":    fmt.Sprintf("A Reasonably Long Skyrim Mod Name Number %d", i),
			"type":    "mod",
			"enabled": true,
			"version": "1.0.0",
		})
	}
	return map[string]any{"ok": true, "mods": mods}
}

// The bug this pins down: list_mods failed with "unexpected end of JSON input"
// on a 696-mod profile while get_summary worked, because the client read the
// response once into a fixed 64 KiB buffer. A real mod list is bigger than
// that, and the truncated bytes are not valid JSON.
func TestListModsReadsAResponseLargerThanOneBuffer(t *testing.T) {
	const modCount = 700
	resp := modsResponse(modCount)
	if b, _ := json.Marshal(resp); len(b) <= 65536 {
		t.Fatalf("test payload is only %d bytes - too small to exercise the bug", len(b))
	}

	c := New(fakeSolero(t, resp))
	got, err := c.ListMods()
	if err != nil {
		t.Fatalf("ListMods: %v", err)
	}
	mods, ok := got["mods"].([]any)
	if !ok {
		t.Fatalf("no mods array in response: %#v", got)
	}
	if len(mods) != modCount {
		t.Errorf("got %d mods, want %d", len(mods), modCount)
	}
}

func TestSmallResponsesStillWork(t *testing.T) {
	c := New(fakeSolero(t, map[string]any{"ok": true, "profile": "Season Unending"}))
	got, err := c.GetSummary()
	if err != nil {
		t.Fatalf("GetSummary: %v", err)
	}
	if got["profile"] != "Season Unending" {
		t.Errorf("got profile %v", got["profile"])
	}
}

func TestErrorResponsesSurfaceTheMessage(t *testing.T) {
	c := New(fakeSolero(t, map[string]any{"ok": false, "error": "no active profile"}))
	if _, err := c.ListMods(); err == nil {
		t.Fatal("want an error")
	} else if got := err.Error(); got != "solero error: no active profile" {
		t.Errorf("got %q", got)
	}
}
