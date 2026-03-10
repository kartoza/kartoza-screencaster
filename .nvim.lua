-- Kartoza Screencaster - Neovim Project Configuration
-- =====================================================
-- Project-specific keybindings and settings for development
-- All project bindings are under <leader>p (Project)

-- Ensure we're in the nix develop environment
local function in_nix_shell()
  return vim.env.IN_NIX_SHELL ~= nil
end

-- Helper to run terminal commands
local function run_term(cmd, opts)
  opts = opts or {}
  local direction = opts.direction or "float"
  local size = opts.size or 0.8

  -- Use toggleterm if available
  local ok, toggleterm = pcall(require, "toggleterm.terminal")
  if ok then
    local Terminal = toggleterm.Terminal
    local term = Terminal:new({
      cmd = cmd,
      direction = direction,
      float_opts = { border = "curved" },
      close_on_exit = false,
      on_open = function(t)
        vim.api.nvim_buf_set_keymap(t.bufnr, "n", "q", "<cmd>close<CR>", { noremap = true, silent = true })
      end,
    })
    term:toggle()
  else
    -- Fallback to built-in terminal
    vim.cmd("split | terminal " .. cmd)
  end
end

-- Helper to run make targets
local function make(target)
  run_term("make " .. target)
end

-- Helper to run nix commands
local function nix_run(app)
  run_term("nix run .#" .. app)
end

-- Setup which-key if available
local function setup_which_key()
  local ok, wk = pcall(require, "which-key")
  if not ok then
    vim.notify("which-key not found, using basic keymaps", vim.log.levels.WARN)
    return false
  end

  -- Register project group
  wk.add({
    { "<leader>p", group = "Project (Kartoza Screencaster)" },

    -- Build commands
    { "<leader>pb", group = "Build" },
    { "<leader>pbb", function() make("build") end, desc = "Build dynamic binary" },
    { "<leader>pbs", function() make("static") end, desc = "Build static binary" },
    { "<leader>pba", function() make("build-all") end, desc = "Build all (test + binaries)" },
    { "<leader>pbc", function() make("clean") end, desc = "Clean build artifacts" },

    -- Run commands
    { "<leader>pr", group = "Run" },
    { "<leader>prr", function() make("dev") end, desc = "Run application (go run)" },
    { "<leader>prn", function() nix_run("default") end, desc = "Run via nix (with systray)" },
    { "<leader>prs", function() nix_run("setup") end, desc = "Run setup script" },

    -- Test commands
    { "<leader>pt", group = "Test" },
    { "<leader>ptt", function() make("test") end, desc = "Run all tests" },
    { "<leader>ptc", function() make("check") end, desc = "Run quality gate (fmt+lint+test)" },
    { "<leader>ptf", function() run_term("go test -v -run " .. vim.fn.expand("<cword>") .. " ./...") end, desc = "Test function under cursor" },
    { "<leader>ptp", function() run_term("go test -v " .. vim.fn.expand("%:h")) end, desc = "Test current package" },

    -- Lint/Format commands
    { "<leader>pl", group = "Lint/Format" },
    { "<leader>pll", function() make("lint") end, desc = "Run golangci-lint" },
    { "<leader>plf", function() make("fmt") end, desc = "Format code (gofmt)" },

    -- Dependencies
    { "<leader>pd", group = "Dependencies" },
    { "<leader>pdd", function() make("deps") end, desc = "Download and tidy deps" },
    { "<leader>pdm", function() run_term("go mod tidy") end, desc = "Go mod tidy" },
    { "<leader>pdg", function() run_term("go get -u ./...") end, desc = "Update all dependencies" },

    -- Documentation
    { "<leader>po", group = "Documentation" },
    { "<leader>pos", function() run_term("mkdocs serve") end, desc = "Serve docs (localhost:8000)" },
    { "<leader>pob", function() run_term("mkdocs build") end, desc = "Build static docs" },
    { "<leader>poo", function() vim.cmd("!xdg-open http://localhost:8000 &") end, desc = "Open docs in browser" },

    -- Release commands
    { "<leader>px", group = "Release" },
    { "<leader>pxr", function() make("release") end, desc = "Build multi-platform release" },
    { "<leader>pxc", function() make("release-clean") end, desc = "Clean release directory" },
    { "<leader>pxn", function() nix_run("release") end, desc = "Build release via nix" },

    -- Package commands
    { "<leader>pk", group = "Packages" },
    { "<leader>pkd", function() make("deb") end, desc = "Build Debian package" },
    { "<leader>pkr", function() make("rpm") end, desc = "Build RPM package" },
    { "<leader>pks", function() make("snap") end, desc = "Build Snap package" },
    { "<leader>pkf", function() make("flatpak") end, desc = "Build Flatpak package" },
    { "<leader>pka", function() make("packages") end, desc = "Build all packages" },

    -- Nix commands
    { "<leader>pn", group = "Nix" },
    { "<leader>pnd", function() run_term("nix develop") end, desc = "Enter nix develop shell" },
    { "<leader>pnb", function() run_term("nix build") end, desc = "Nix build default" },
    { "<leader>pnf", function() run_term("nix flake check") end, desc = "Check flake" },
    { "<leader>pnu", function() run_term("nix flake update") end, desc = "Update flake inputs" },
    { "<leader>pns", function() run_term("nix flake show") end, desc = "Show flake outputs" },

    -- Git/GitHub commands
    { "<leader>pg", group = "Git/GitHub" },
    { "<leader>pgs", function() run_term("git status") end, desc = "Git status" },
    { "<leader>pgp", function() run_term("gh pr list") end, desc = "List PRs" },
    { "<leader>pgi", function() run_term("gh issue list") end, desc = "List issues" },
    { "<leader>pgr", function() run_term("gh release list") end, desc = "List releases" },

    -- Debug commands (Delve)
    { "<leader>pD", group = "Debug" },
    { "<leader>pDd", function() run_term("dlv debug . -- ") end, desc = "Debug application (dlv debug)" },
    { "<leader>pDs", function() run_term("dlv debug . -- systray") end, desc = "Debug systray mode" },
    { "<leader>pDt", function() run_term("dlv test ./...") end, desc = "Debug all tests" },
    { "<leader>pDf", function() run_term("dlv test " .. vim.fn.expand("%:h") .. " -- -test.run " .. vim.fn.expand("<cword>")) end, desc = "Debug test under cursor" },
    { "<leader>pDp", function() run_term("dlv test " .. vim.fn.expand("%:h")) end, desc = "Debug current package tests" },
    { "<leader>pDa", function()
      vim.ui.input({ prompt = "PID to attach: " }, function(pid)
        if pid then run_term("dlv attach " .. pid) end
      end)
    end, desc = "Attach to running process" },
    { "<leader>pDc", function() run_term("dlv connect 127.0.0.1:2345") end, desc = "Connect to headless debugger" },
    { "<leader>pDh", function() run_term("dlv debug . --headless --listen=:2345 --api-version=2") end, desc = "Start headless debugger (for DAP)" },

    -- Info commands
    { "<leader>pi", group = "Info" },
    { "<leader>pii", function() make("info") end, desc = "Show build info" },
    { "<leader>pih", function() make("help") end, desc = "Show Makefile help" },
    { "<leader>pic", function() run_term("go version && echo '' && go env") end, desc = "Go environment info" },
  })

  return true
end

-- Fallback keymaps if which-key is not available
local function setup_basic_keymaps()
  local opts = { noremap = true, silent = true }

  -- Build
  vim.keymap.set("n", "<leader>pbb", ":!make build<CR>", opts)
  vim.keymap.set("n", "<leader>pbs", ":!make static<CR>", opts)
  vim.keymap.set("n", "<leader>pbc", ":!make clean<CR>", opts)

  -- Run
  vim.keymap.set("n", "<leader>prr", ":!make dev<CR>", opts)

  -- Test
  vim.keymap.set("n", "<leader>ptt", ":!make test<CR>", opts)
  vim.keymap.set("n", "<leader>ptc", ":!make check<CR>", opts)

  -- Lint
  vim.keymap.set("n", "<leader>pll", ":!make lint<CR>", opts)
  vim.keymap.set("n", "<leader>plf", ":!make fmt<CR>", opts)

  -- Docs
  vim.keymap.set("n", "<leader>pos", ":!mkdocs serve &<CR>", opts)

  -- Info
  vim.keymap.set("n", "<leader>pih", ":!make help<CR>", opts)
end

-- Setup nvim-dap for Go debugging (if available)
local function setup_dap()
  local dap_ok, dap = pcall(require, "dap")
  if not dap_ok then
    return false
  end

  -- Configure delve adapter
  dap.adapters.delve = {
    type = "server",
    port = "${port}",
    executable = {
      command = "dlv",
      args = { "dap", "-l", "127.0.0.1:${port}" },
    },
  }

  -- Go debug configurations
  dap.configurations.go = {
    {
      type = "delve",
      name = "Debug Application",
      request = "launch",
      program = "${workspaceFolder}",
    },
    {
      type = "delve",
      name = "Debug Systray Mode",
      request = "launch",
      program = "${workspaceFolder}",
      args = { "systray" },
    },
    {
      type = "delve",
      name = "Debug Current Test",
      request = "launch",
      mode = "test",
      program = "${fileDirname}",
    },
    {
      type = "delve",
      name = "Debug All Tests",
      request = "launch",
      mode = "test",
      program = "./...",
    },
    {
      type = "delve",
      name = "Attach to Process",
      request = "attach",
      mode = "local",
      processId = require("dap.utils").pick_process,
    },
  }

  -- Setup DAP keybindings under <leader>pd (already used for deps, use <leader>pD for debug)
  local wk_ok, wk = pcall(require, "which-key")
  if wk_ok then
    wk.add({
      { "<leader>pDb", function() dap.toggle_breakpoint() end, desc = "Toggle breakpoint" },
      { "<leader>pDB", function() dap.set_breakpoint(vim.fn.input("Condition: ")) end, desc = "Conditional breakpoint" },
      { "<leader>pDr", function() dap.continue() end, desc = "Start/Continue debugging" },
      { "<leader>pDn", function() dap.step_over() end, desc = "Step over" },
      { "<leader>pDi", function() dap.step_into() end, desc = "Step into" },
      { "<leader>pDo", function() dap.step_out() end, desc = "Step out" },
      { "<leader>pDq", function() dap.terminate() end, desc = "Stop debugging" },
      { "<leader>pDR", function() dap.repl.open() end, desc = "Open REPL" },
      { "<leader>pDl", function() dap.run_last() end, desc = "Run last configuration" },
    })
  end

  -- Setup dap-ui if available
  local dapui_ok, dapui = pcall(require, "dapui")
  if dapui_ok then
    dapui.setup()
    dap.listeners.after.event_initialized["dapui_config"] = function()
      dapui.open()
    end
    dap.listeners.before.event_terminated["dapui_config"] = function()
      dapui.close()
    end
    dap.listeners.before.event_exited["dapui_config"] = function()
      dapui.close()
    end

    if wk_ok then
      wk.add({
        { "<leader>pDu", function() dapui.toggle() end, desc = "Toggle DAP UI" },
        { "<leader>pDe", function() dapui.eval() end, desc = "Evaluate expression" },
      })
    end
  end

  return true
end

-- Go-specific settings
local function setup_go_settings()
  -- Set up Go-specific options
  vim.api.nvim_create_autocmd("FileType", {
    pattern = "go",
    callback = function()
      vim.opt_local.tabstop = 4
      vim.opt_local.shiftwidth = 4
      vim.opt_local.expandtab = false

      -- Go to definition with gopls
      vim.keymap.set("n", "gd", vim.lsp.buf.definition, { buffer = true, desc = "Go to definition" })
      vim.keymap.set("n", "gr", vim.lsp.buf.references, { buffer = true, desc = "Find references" })
      vim.keymap.set("n", "K", vim.lsp.buf.hover, { buffer = true, desc = "Hover documentation" })
    end,
  })
end

-- Project-specific settings
local function setup_project_settings()
  -- Set project root markers
  vim.opt.path:append("**")

  -- Ignore build directories in searches
  vim.opt.wildignore:append({
    "*.exe",
    "bin/*",
    "release/*",
    ".go/*",
    "site/*",
    "*.tar.gz",
  })

  -- Set up error format for Go
  vim.opt.errorformat = "%f:%l:%c: %m,%f:%l: %m"

  -- Auto-format on save for Go files (if gofmt is available)
  vim.api.nvim_create_autocmd("BufWritePre", {
    pattern = "*.go",
    callback = function()
      -- Use LSP formatting if available
      local clients = vim.lsp.get_clients({ bufnr = 0 })
      for _, client in ipairs(clients) do
        if client.name == "gopls" then
          vim.lsp.buf.format({ async = false, timeout_ms = 3000 })
          return
        end
      end
    end,
  })
end

-- Initialize all configurations
local function init()
  -- Set up which-key bindings or fallback
  if not setup_which_key() then
    setup_basic_keymaps()
  end

  -- Set up DAP debugging (adds keybindings if nvim-dap is available)
  local dap_available = setup_dap()

  -- Set up Go-specific settings
  setup_go_settings()

  -- Set up project settings
  setup_project_settings()

  -- Notify user
  local msg = "Kartoza Screencaster project loaded. Use <leader>p for project commands."
  if dap_available then
    msg = msg .. " DAP debugging enabled."
  end
  vim.notify(msg, vim.log.levels.INFO)
end

-- Run initialization
init()

-- Export for testing/debugging
return {
  run_term = run_term,
  make = make,
  nix_run = nix_run,
}
