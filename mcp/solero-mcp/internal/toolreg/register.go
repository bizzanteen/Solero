package toolreg

import (
	"github.com/modelcontextprotocol/go-sdk/mcp"
	"github.com/solero/solero-mcp/internal/soleroclient"
	"github.com/solero/solero-mcp/internal/tools"
)

func Register(server *mcp.Server) {
	client := soleroclient.New("")

	mcp.AddTool(server, &mcp.Tool{
		Name:        "list_mods",
		Description: "List all mods in the active Solero profile, including separators and their order.",
	}, tools.ListMods(client))

	mcp.AddTool(server, &mcp.Tool{
		Name:        "list_plugins",
		Description: "List all plugins (.esp/.esm/.esl) in the active profile with their load order.",
	}, tools.ListPlugins(client))

	mcp.AddTool(server, &mcp.Tool{
		Name:        "get_profile_summary",
		Description: "Get a summary of the active Solero profile: name, mod count, plugin count.",
	}, tools.GetProfileSummary(client))

	mcp.AddTool(server, &mcp.Tool{
		Name:        "list_ai_transactions",
		Description: "List all AI-applied changes with their IDs. Use ai_revert to undo any of them.",
	}, tools.ListAITransactions(client))

	mcp.AddTool(server, &mcp.Tool{
		Name:        "ai_revert",
		Description: "Revert a specific AI transaction by ID, restoring the profile to its pre-change state. This is always safe - it restores exact file snapshots.",
	}, tools.AIRevert(client))

	mcp.AddTool(server, &mcp.Tool{
		Name:        "enable_mod",
		Description: "Enable or disable a mod in the active profile. Changes take effect on next deploy. Tracked as an AI transaction - revert with ai_revert.",
	}, tools.EnableMod(client))

	mcp.AddTool(server, &mcp.Tool{
		Name:        "move_mod",
		Description: "Move a mod to a different priority position (0 = lowest priority). Higher priority mods overwrite lower priority mods' files. Tracked - revert with ai_revert.",
	}, tools.MoveMod(client))
}
