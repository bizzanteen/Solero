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
}
