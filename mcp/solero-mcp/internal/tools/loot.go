package tools

import (
	"context"
	"encoding/json"
	"fmt"

	"github.com/modelcontextprotocol/go-sdk/mcp"
	"github.com/solero/solero-mcp/internal/soleroclient"
)

func ListLootRules(client *soleroclient.Client) func(context.Context, *mcp.CallToolRequest, struct{}) (*mcp.CallToolResult, any, error) {
	return func(_ context.Context, _ *mcp.CallToolRequest, _ struct{}) (*mcp.CallToolResult, any, error) {
		resp, err := client.ListLootRules()
		if err != nil {
			return toolErr(err.Error()), nil, nil
		}
		b, _ := json.MarshalIndent(resp, "", "  ")
		return &mcp.CallToolResult{
			Content: []mcp.Content{&mcp.TextContent{Text: fmt.Sprintf("LOOT rules for the active profile:\n%s", string(b))}},
		}, nil, nil
	}
}

type lootRuleArgs struct {
	Plugin string `json:"plugin" jsonschema:"The plugin the rule is about, e.g. SonS_HDT_patch.esp"`
	Type   string `json:"type"   jsonschema:"Rule type: after (load after target), req (requires target), inc (incompatible with target), or group (assign the plugin to a LOOT group)"`
	Target string `json:"target" jsonschema:"The other plugin's filename, or - for type=group - the group name (case-sensitive)"`
}

func SetLootRule(client *soleroclient.Client) func(context.Context, *mcp.CallToolRequest, lootRuleArgs) (*mcp.CallToolResult, any, error) {
	return func(_ context.Context, _ *mcp.CallToolRequest, in lootRuleArgs) (*mcp.CallToolResult, any, error) {
		resp, err := client.SetLootRule(in.Plugin, in.Type, in.Target)
		if err != nil {
			return toolErr(err.Error()), nil, nil
		}
		b, _ := json.MarshalIndent(resp, "", "  ")
		return &mcp.CallToolResult{
			Content: []mcp.Content{&mcp.TextContent{Text: string(b)}},
		}, nil, nil
	}
}

func DeleteLootRule(client *soleroclient.Client) func(context.Context, *mcp.CallToolRequest, lootRuleArgs) (*mcp.CallToolResult, any, error) {
	return func(_ context.Context, _ *mcp.CallToolRequest, in lootRuleArgs) (*mcp.CallToolResult, any, error) {
		resp, err := client.DeleteLootRule(in.Plugin, in.Type, in.Target)
		if err != nil {
			return toolErr(err.Error()), nil, nil
		}
		b, _ := json.MarshalIndent(resp, "", "  ")
		return &mcp.CallToolResult{
			Content: []mcp.Content{&mcp.TextContent{Text: string(b)}},
		}, nil, nil
	}
}

func SortPlugins(client *soleroclient.Client) func(context.Context, *mcp.CallToolRequest, struct{}) (*mcp.CallToolResult, any, error) {
	return func(_ context.Context, _ *mcp.CallToolRequest, _ struct{}) (*mcp.CallToolResult, any, error) {
		resp, err := client.SortPlugins()
		if err != nil {
			return toolErr(err.Error()), nil, nil
		}
		b, _ := json.MarshalIndent(resp, "", "  ")
		return &mcp.CallToolResult{
			Content: []mcp.Content{&mcp.TextContent{Text: string(b)}},
		}, nil, nil
	}
}
