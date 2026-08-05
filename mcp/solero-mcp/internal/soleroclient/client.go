package soleroclient

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io"
	"net"
)

// Solero frames every response as one line of JSON. A full list_mods on a large
// profile runs to hundreds of KB, so the only real limit is a backstop against
// a peer that never sends the newline.
const maxResponseBytes = 64 << 20 // 64 MiB

type Client struct {
	socketPath string
}

func New(socketPath string) *Client {
	if socketPath == "" {
		socketPath = "/tmp/solero-ipc"
	}
	return &Client{socketPath: socketPath}
}

func (c *Client) call(req map[string]any) (map[string]any, error) {
	conn, err := net.Dial("unix", c.socketPath)
	if err != nil {
		return nil, fmt.Errorf("solero not running (socket %s): %w", c.socketPath, err)
	}
	defer conn.Close()

	data, err := json.Marshal(req)
	if err != nil {
		return nil, fmt.Errorf("encoding request: %w", err)
	}
	if _, err := conn.Write(append(data, '\n')); err != nil {
		return nil, fmt.Errorf("sending request: %w", err)
	}

	// Read to the end of the frame rather than taking whatever the first read
	// happened to return: a single fixed read truncated large responses, and
	// the leftover bytes are not valid JSON.
	line, err := bufio.NewReader(io.LimitReader(conn, maxResponseBytes)).ReadBytes('\n')
	if err != nil {
		if int64(len(line)) >= maxResponseBytes {
			return nil, fmt.Errorf("response exceeded %d bytes", maxResponseBytes)
		}
		return nil, fmt.Errorf("reading response: %w", err)
	}

	var resp map[string]any
	if err := json.Unmarshal(line, &resp); err != nil {
		return nil, fmt.Errorf("parsing response: %w", err)
	}
	if ok, _ := resp["ok"].(bool); !ok {
		return nil, fmt.Errorf("solero error: %v", resp["error"])
	}
	return resp, nil
}

func (c *Client) ListMods() (map[string]any, error) {
	return c.call(map[string]any{"action": "list_mods"})
}

func (c *Client) ListPlugins() (map[string]any, error) {
	return c.call(map[string]any{"action": "list_plugins"})
}

func (c *Client) GetSummary() (map[string]any, error) {
	return c.call(map[string]any{"action": "get_summary"})
}

func (c *Client) ListTransactions() (map[string]any, error) {
	return c.call(map[string]any{"action": "list_transactions"})
}

func (c *Client) AIWrite(description string, changes []map[string]any) (map[string]any, error) {
	return c.call(map[string]any{
		"action":      "ai_write",
		"description": description,
		"changes":     changes,
	})
}

func (c *Client) AIRevert(transactionID string) (map[string]any, error) {
	return c.call(map[string]any{
		"action":        "ai_revert",
		"transactionId": transactionID,
	})
}

func (c *Client) EnableMod(modID string, enabled bool) (map[string]any, error) {
	return c.call(map[string]any{"action": "enable_mod", "modId": modID, "enabled": enabled})
}

func (c *Client) MoveMod(from, to int) (map[string]any, error) {
	return c.call(map[string]any{"action": "move_mod", "from": from, "to": to})
}

func (c *Client) ListLootRules() (map[string]any, error) {
	return c.call(map[string]any{"action": "list_loot_rules"})
}

// ruleType is one of after / req / inc / group.
func (c *Client) SetLootRule(plugin, ruleType, target string) (map[string]any, error) {
	return c.call(map[string]any{
		"action": "set_loot_rule",
		"plugin": plugin,
		"type":   ruleType,
		"target": target,
	})
}

func (c *Client) DeleteLootRule(plugin, ruleType, target string) (map[string]any, error) {
	return c.call(map[string]any{
		"action": "delete_loot_rule",
		"plugin": plugin,
		"type":   ruleType,
		"target": target,
	})
}

func (c *Client) SortPlugins() (map[string]any, error) {
	return c.call(map[string]any{"action": "sort_plugins"})
}
