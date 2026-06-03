package soleroclient

import (
	"encoding/json"
	"fmt"
	"net"
)

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

	data, _ := json.Marshal(req)
	conn.Write(append(data, '\n'))

	buf := make([]byte, 65536)
	n, err := conn.Read(buf)
	if err != nil {
		return nil, fmt.Errorf("reading response: %w", err)
	}

	var resp map[string]any
	if err := json.Unmarshal(buf[:n], &resp); err != nil {
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
