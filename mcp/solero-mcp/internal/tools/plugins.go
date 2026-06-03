package tools

import (
	"context"
	"encoding/json"
	"fmt"

	"github.com/modelcontextprotocol/go-sdk/mcp"
	"github.com/solero/solero-mcp/internal/soleroclient"
)

func ListPlugins(client *soleroclient.Client) func(context.Context, *mcp.CallToolRequest, struct{}) (*mcp.CallToolResult, any, error) {
	return func(_ context.Context, _ *mcp.CallToolRequest, _ struct{}) (*mcp.CallToolResult, any, error) {
		resp, err := client.ListPlugins()
		if err != nil {
			return toolErr(err.Error()), nil, nil
		}
		b, _ := json.MarshalIndent(resp["plugins"], "", "  ")
		return &mcp.CallToolResult{
			Content: []mcp.Content{&mcp.TextContent{Text: fmt.Sprintf("Plugins in active profile:\n%s", string(b))}},
		}, nil, nil
	}
}
