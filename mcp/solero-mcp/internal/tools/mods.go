package tools

import (
	"context"
	"encoding/json"
	"fmt"

	"github.com/modelcontextprotocol/go-sdk/mcp"
	"github.com/solero/solero-mcp/internal/soleroclient"
)

func ListMods(client *soleroclient.Client) func(context.Context, *mcp.CallToolRequest, struct{}) (*mcp.CallToolResult, any, error) {
	return func(_ context.Context, _ *mcp.CallToolRequest, _ struct{}) (*mcp.CallToolResult, any, error) {
		resp, err := client.ListMods()
		if err != nil {
			return toolErr(err.Error()), nil, nil
		}
		b, _ := json.MarshalIndent(resp["mods"], "", "  ")
		return &mcp.CallToolResult{
			Content: []mcp.Content{&mcp.TextContent{Text: fmt.Sprintf("Mods in active profile:\n%s", string(b))}},
		}, nil, nil
	}
}

type aiRevertArgs struct {
	TransactionID string `json:"transaction_id" jsonschema:"The transaction ID to revert (from list_ai_transactions)"`
}

func AIRevert(client *soleroclient.Client) func(context.Context, *mcp.CallToolRequest, aiRevertArgs) (*mcp.CallToolResult, any, error) {
	return func(_ context.Context, _ *mcp.CallToolRequest, in aiRevertArgs) (*mcp.CallToolResult, any, error) {
		resp, err := client.AIRevert(in.TransactionID)
		if err != nil {
			return toolErr(err.Error()), nil, nil
		}
		b, _ := json.MarshalIndent(resp, "", "  ")
		return &mcp.CallToolResult{
			Content: []mcp.Content{&mcp.TextContent{Text: string(b)}},
		}, nil, nil
	}
}

type enableModArgs struct {
	ModID   string `json:"mod_id"  jsonschema:"The mod's id field from list_mods"`
	Enabled bool   `json:"enabled" jsonschema:"true to enable the mod, false to disable"`
}

func EnableMod(client *soleroclient.Client) func(context.Context, *mcp.CallToolRequest, enableModArgs) (*mcp.CallToolResult, any, error) {
	return func(_ context.Context, _ *mcp.CallToolRequest, in enableModArgs) (*mcp.CallToolResult, any, error) {
		resp, err := client.EnableMod(in.ModID, in.Enabled)
		if err != nil {
			return toolErr(err.Error()), nil, nil
		}
		b, _ := json.MarshalIndent(resp, "", "  ")
		return &mcp.CallToolResult{
			Content: []mcp.Content{&mcp.TextContent{Text: string(b)}},
		}, nil, nil
	}
}

type moveModArgs struct {
	From int `json:"from" jsonschema:"Current priority index (0 = lowest priority)"`
	To   int `json:"to"   jsonschema:"Target priority index"`
}

func MoveMod(client *soleroclient.Client) func(context.Context, *mcp.CallToolRequest, moveModArgs) (*mcp.CallToolResult, any, error) {
	return func(_ context.Context, _ *mcp.CallToolRequest, in moveModArgs) (*mcp.CallToolResult, any, error) {
		resp, err := client.MoveMod(in.From, in.To)
		if err != nil {
			return toolErr(err.Error()), nil, nil
		}
		b, _ := json.MarshalIndent(resp, "", "  ")
		return &mcp.CallToolResult{
			Content: []mcp.Content{&mcp.TextContent{Text: string(b)}},
		}, nil, nil
	}
}

func toolErr(msg string) *mcp.CallToolResult {
	return &mcp.CallToolResult{
		Content: []mcp.Content{&mcp.TextContent{Text: msg}},
		IsError: true,
	}
}
