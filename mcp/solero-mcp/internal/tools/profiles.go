package tools

import (
	"context"
	"encoding/json"
	"fmt"

	"github.com/modelcontextprotocol/go-sdk/mcp"
	"github.com/solero/solero-mcp/internal/soleroclient"
)

func GetProfileSummary(client *soleroclient.Client) func(context.Context, *mcp.CallToolRequest, struct{}) (*mcp.CallToolResult, any, error) {
	return func(_ context.Context, _ *mcp.CallToolRequest, _ struct{}) (*mcp.CallToolResult, any, error) {
		resp, err := client.GetSummary()
		if err != nil {
			return toolErr(err.Error()), nil, nil
		}
		b, _ := json.MarshalIndent(resp, "", "  ")
		return &mcp.CallToolResult{
			Content: []mcp.Content{&mcp.TextContent{Text: fmt.Sprintf("Solero profile summary:\n%s", string(b))}},
		}, nil, nil
	}
}

func ListAITransactions(client *soleroclient.Client) func(context.Context, *mcp.CallToolRequest, struct{}) (*mcp.CallToolResult, any, error) {
	return func(_ context.Context, _ *mcp.CallToolRequest, _ struct{}) (*mcp.CallToolResult, any, error) {
		resp, err := client.ListTransactions()
		if err != nil {
			return toolErr(err.Error()), nil, nil
		}
		b, _ := json.MarshalIndent(resp["transactions"], "", "  ")
		return &mcp.CallToolResult{
			Content: []mcp.Content{&mcp.TextContent{Text: fmt.Sprintf("AI transactions (revert any with ai_revert):\n%s", string(b))}},
		}, nil, nil
	}
}
