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

	mcp.AddTool(server, &mcp.Tool{
		Name: "list_loot_rules",
		Description: "List the active profile's LOOT userlist rules: per-plugin after/req/inc targets and group assignments, " +
			"plus availableGroups - every group name a group rule may legally use. Read this before calling set_loot_rule with type=group.",
	}, tools.ListLootRules(client))

	mcp.AddTool(server, &mcp.Tool{
		Name: "set_loot_rule",
		Description: "Add or update one LOOT rule for a plugin (type: after, req, inc or group). Idempotent - re-adding an existing rule is a no-op. " +
			"Group names are case-sensitive and are rejected unless they exactly match a name from list_loot_rules' availableGroups, because an " +
			"undefined group makes LOOT abort the entire sort. Rules only take effect on the next sort_plugins. Tracked - revert with ai_revert.",
	}, tools.SetLootRule(client))

	mcp.AddTool(server, &mcp.Tool{
		Name:        "delete_loot_rule",
		Description: "Remove one LOOT rule, identified by plugin + type + target. Removing a rule that isn't there reports changed=false rather than failing. Tracked - revert with ai_revert.",
	}, tools.DeleteLootRule(client))

	mcp.AddTool(server, &mcp.Tool{
		Name: "sort_plugins",
		Description: "Run a LOOT sort now and return the resulting load order, or the error if it failed. Saving a rule does not re-sort, so call this to apply " +
			"what set_loot_rule just wrote. Needs the profile deployed (LOOT reads the plugins in the game's Data folder). If the profile's load order is locked " +
			"this refuses unless force=true; only force when the user asked for the sort. Snapshots the pre-sort order first, restorable from Solero's " +
			"Restore Load Order.",
	}, tools.SortPlugins(client))
}
