-- Kartoza Screencaster - Neovim Project Configuration (C++ / Qt6)
-- ================================================================
-- All project bindings under <leader>p (Project)

-- C++ file settings: 2-space indent, no tabs
vim.api.nvim_create_autocmd("FileType", {
  pattern = { "cpp", "c", "h", "hpp" },
  callback = function()
    vim.opt_local.tabstop = 2
    vim.opt_local.shiftwidth = 2
    vim.opt_local.expandtab = true
    vim.opt_local.cinoptions = "g0,N-s,i2s,+2s,(0,w1,W2s"
  end,
})

-- Auto-format C++ on save with clang-format
vim.api.nvim_create_autocmd("BufWritePre", {
  pattern = { "*.cpp", "*.h", "*.hpp", "*.cc" },
  callback = function()
    local clients = vim.lsp.get_clients({ bufnr = 0 })
    for _, client in ipairs(clients) do
      if client.name == "clangd" then
        vim.lsp.buf.format({ async = false, timeout_ms = 3000 })
        return
      end
    end
  end,
})

-- CMake file settings
vim.api.nvim_create_autocmd("FileType", {
  pattern = "cmake",
  callback = function()
    vim.opt_local.tabstop = 2
    vim.opt_local.shiftwidth = 2
    vim.opt_local.expandtab = true
  end,
})

-- Ignore build artifacts in searches
vim.opt.wildignore:append({
  "build/*", "*.o", "*.moc", "moc_*.cpp", "*_autogen/*",
  "*.so", "*.a", "*.dylib",
})

-- Export compile_commands.json symlink for clangd
vim.api.nvim_create_autocmd("VimEnter", {
  callback = function()
    local cc = vim.fn.getcwd() .. "/build/compile_commands.json"
    local link = vim.fn.getcwd() .. "/compile_commands.json"
    if vim.fn.filereadable(cc) == 1 and vim.fn.filereadable(link) == 0 then
      vim.fn.system("ln -sf build/compile_commands.json compile_commands.json")
    end
  end,
})

-- Helper: run a terminal command
local function run_term(cmd, opts)
  opts = opts or {}
  local ok, toggleterm = pcall(require, "toggleterm.terminal")
  if ok then
    local Terminal = toggleterm.Terminal
    Terminal:new({
      cmd = cmd,
      direction = opts.direction or "float",
      float_opts = { border = "curved" },
      close_on_exit = false,
      on_open = function(t)
        vim.api.nvim_buf_set_keymap(t.bufnr, "n", "q", "<cmd>close<CR>", { noremap = true, silent = true })
      end,
    }):toggle()
  else
    vim.cmd("split | terminal " .. cmd)
  end
end

-- Helper: build via ksc-dev — the single build path (timing + build-log.tsv +
-- ccache stats -> cmake+ninja -> CMakeLists.txt ccache/mold). Keep all build
-- behaviour in scripts/ksc-dev.sh, not here.
local function cmake_build(build_type)
  run_term("ksc-dev " .. (build_type == "Release" and "release" or "build"))
end

-- Setup which-key
local function setup_which_key()
  local ok, wk = pcall(require, "which-key")
  if not ok then return false end

  wk.add({
    { "<leader>p", group = "Project (Screencaster C++)" },

    -- Build
    { "<leader>pb", group = "Build" },
    { "<leader>pbb", function() cmake_build("Debug") end, desc = "Build (Debug) [ksc-dev]" },
    { "<leader>pbr", function() cmake_build("Release") end, desc = "Build (Release) [ksc-dev]" },
    { "<leader>pbc", function() run_term("ksc-dev clean") end, desc = "Clean rebuild [ksc-dev]" },
    { "<leader>pbf", function() run_term("ksc-dev build") end, desc = "Reconfigure + build [ksc-dev]" },
    { "<leader>pbs", function() run_term("ksc-dev stats --graph") end, desc = "Build stats (durations, ccache hit rate)" },

    -- Run
    { "<leader>pr", group = "Run" },
    { "<leader>prr", function() run_term("ksc-dev run") end, desc = "Run application [ksc-dev]" },
    { "<leader>prq", function() run_term("QT_LOGGING_RULES='*.debug=true' ./build/kartoza-screencaster") end, desc = "Run with Qt debug logging" },

    -- Test
    { "<leader>pt", group = "Test" },
    { "<leader>ptt", function() run_term("ksc-dev test") end, desc = "Run all tests [ksc-dev]" },
    { "<leader>ptm", function() run_term("cd build && ./test_merger -v1") end, desc = "Run merger tests" },
    { "<leader>ptc", function() run_term("cd build && ./test_config -v1") end, desc = "Run config tests" },
    { "<leader>pto", function() run_term("cd build && ./test_monitor -v1") end, desc = "Run monitor tests" },
    { "<leader>ptp", function() run_term("cd build && ./test_pipeline -v1") end, desc = "Run pipeline tests" },

    -- Format / Lint
    { "<leader>pl", group = "Lint/Format" },
    { "<leader>plf", function() run_term("ksc-dev format") end, desc = "Format all C++ (clang-format) [ksc-dev]" },
    { "<leader>plt", function() run_term("cd build && run-clang-tidy -p . ../src/") end, desc = "Run clang-tidy" },
    { "<leader>plc", function() run_term("cppcheck --enable=all --suppress=missingInclude -I src/ src/") end, desc = "Run cppcheck" },

    -- Debug (GDB)
    { "<leader>pD", group = "Debug" },
    { "<leader>pDd", function() run_term("gdb -q ./build/kartoza-screencaster") end, desc = "Debug with GDB" },
    { "<leader>pDt", function()
      local test = vim.fn.input("Test binary (merger/config/monitor/pipeline): ")
      run_term("gdb -q ./build/test_" .. test)
    end, desc = "Debug test binary" },
    { "<leader>pDa", function()
      vim.ui.input({ prompt = "PID: " }, function(pid)
        if pid then run_term("gdb -q -p " .. pid) end
      end)
    end, desc = "Attach to PID" },

    -- Performance / Profiling
    { "<leader>pP", group = "Profile/Perf" },
    { "<leader>pPv", function() run_term("valgrind --leak-check=full --show-leak-kinds=all ./build/kartoza-screencaster") end, desc = "Valgrind leak check" },
    { "<leader>pPc", function() run_term("valgrind --tool=callgrind ./build/kartoza-screencaster") end, desc = "Callgrind profiling" },
    { "<leader>pPh", function() run_term("valgrind --tool=cachegrind ./build/kartoza-screencaster") end, desc = "Cachegrind analysis" },
    { "<leader>pPm", function() run_term("valgrind --tool=massif ./build/kartoza-screencaster") end, desc = "Massif heap profiling" },

    -- Documentation
    { "<leader>po", group = "Documentation" },
    { "<leader>pod", function() run_term("doxygen Doxyfile && xdg-open docs/doxygen/html/index.html") end, desc = "Generate + open Doxygen" },
    { "<leader>pos", function() run_term("mkdocs serve") end, desc = "Serve mkdocs (localhost:8000)" },

    -- Nix
    { "<leader>pn", group = "Nix" },
    { "<leader>pnb", function() run_term("nix build") end, desc = "Nix build" },
    { "<leader>pnf", function() run_term("nix flake check") end, desc = "Check flake" },
    { "<leader>pnu", function() run_term("nix flake update") end, desc = "Update flake inputs" },

    -- Git/GitHub
    { "<leader>pg", group = "Git/GitHub" },
    { "<leader>pgs", function() run_term("git status") end, desc = "Git status" },
    { "<leader>pgp", function() run_term("gh pr list") end, desc = "List PRs" },
    { "<leader>pgi", function() run_term("gh issue list") end, desc = "List issues" },

    -- Info
    { "<leader>pi", group = "Info" },
    { "<leader>pii", function() run_term("cmake --version && ninja --version && ccache -s") end, desc = "Build tool versions" },
    { "<leader>pis", function() run_term("wc -l src/**/*.cpp src/**/*.h | sort -rn | head -20") end, desc = "Source file sizes" },
  })
  return true
end

-- Setup nvim-dap for C++ debugging
local function setup_dap()
  local dap_ok, dap = pcall(require, "dap")
  if not dap_ok then return false end

  dap.adapters.cppdbg = {
    id = "cppdbg",
    type = "executable",
    command = "gdb",
    args = { "-i", "dap" },
  }

  dap.configurations.cpp = {
    {
      name = "Debug Application",
      type = "cppdbg",
      request = "launch",
      program = "${workspaceFolder}/build/kartoza-screencaster",
      cwd = "${workspaceFolder}",
      stopOnEntry = false,
    },
    {
      name = "Debug Test (merger)",
      type = "cppdbg",
      request = "launch",
      program = "${workspaceFolder}/build/test_merger",
      cwd = "${workspaceFolder}/build",
    },
    {
      name = "Debug Test (pipeline)",
      type = "cppdbg",
      request = "launch",
      program = "${workspaceFolder}/build/test_pipeline",
      cwd = "${workspaceFolder}/build",
    },
    {
      name = "Attach to Process",
      type = "cppdbg",
      request = "attach",
      processId = require("dap.utils").pick_process,
    },
  }

  local wk_ok, wk = pcall(require, "which-key")
  if wk_ok then
    wk.add({
      { "<leader>pDb", function() dap.toggle_breakpoint() end, desc = "Toggle breakpoint" },
      { "<leader>pDB", function() dap.set_breakpoint(vim.fn.input("Condition: ")) end, desc = "Conditional breakpoint" },
      { "<leader>pDr", function() dap.continue() end, desc = "Start/Continue" },
      { "<leader>pDn", function() dap.step_over() end, desc = "Step over" },
      { "<leader>pDi", function() dap.step_into() end, desc = "Step into" },
      { "<leader>pDo", function() dap.step_out() end, desc = "Step out" },
      { "<leader>pDq", function() dap.terminate() end, desc = "Stop debugging" },
      { "<leader>pDR", function() dap.repl.open() end, desc = "Open REPL" },
    })
  end

  local dapui_ok, dapui = pcall(require, "dapui")
  if dapui_ok then
    dapui.setup()
    dap.listeners.after.event_initialized["dapui_config"] = function() dapui.open() end
    dap.listeners.before.event_terminated["dapui_config"] = function() dapui.close() end
    if wk_ok then
      wk.add({
        { "<leader>pDu", function() dapui.toggle() end, desc = "Toggle DAP UI" },
        { "<leader>pDe", function() dapui.eval() end, desc = "Evaluate expression" },
      })
    end
  end

  return true
end

-- Initialize
local function init()
  if not setup_which_key() then
    -- Minimal fallback keymaps
    local o = { noremap = true, silent = true }
    vim.keymap.set("n", "<leader>pbb", ":!cd build && ninja<CR>", o)
    vim.keymap.set("n", "<leader>prr", ":!./build/kartoza-screencaster<CR>", o)
    vim.keymap.set("n", "<leader>ptt", ":!cd build && ctest<CR>", o)
  end

  local dap_ok = setup_dap()

  local msg = "Kartoza Screencaster (C++/Qt6) loaded. <leader>p for commands."
  if dap_ok then msg = msg .. " DAP enabled." end
  vim.notify(msg, vim.log.levels.INFO)
end

init()
