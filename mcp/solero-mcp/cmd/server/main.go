package main

import (
	"context"
	"log"
	"net/http"
	"os"
	"strings"

	"github.com/modelcontextprotocol/go-sdk/mcp"
	"github.com/solero/solero-mcp/internal/toolreg"
)

func main() {
	server := mcp.NewServer(&mcp.Implementation{
		Name:    "solero-mcp",
		Version: "0.1.0",
	}, nil)
	toolreg.Register(server)

	mode := strings.ToLower(strings.TrimSpace(os.Getenv("MCP_TRANSPORT")))
	if mode == "" || mode == "stdio" {
		if err := server.Run(context.Background(), &mcp.StdioTransport{}); err != nil {
			log.Fatalf("server: %v", err)
		}
		return
	}
	addr := strings.TrimSpace(os.Getenv("MCP_HTTP_ADDR"))
	if addr == "" {
		addr = ":8090"
	}
	handler := mcp.NewStreamableHTTPHandler(func(_ *http.Request) *mcp.Server { return server }, nil)
	log.Printf("solero-mcp HTTP on %s", addr)
	log.Fatal(http.ListenAndServe(addr, handler))
}
