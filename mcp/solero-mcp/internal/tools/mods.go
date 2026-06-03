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

func toolErr(msg string) *mcp.CallToolResult {
	return &mcp.CallToolResult{
		Content: []mcp.Content{&mcp.TextContent{Text: msg}},
		IsError: true,
	}
}
