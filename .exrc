" Kartoza Screencaster - Vim Project Configuration
" =================================================
" Project-specific keybindings under <leader>p (Project)
" Requires: which-key.nvim or manual loading

" Set local options
set tabstop=4
set shiftwidth=4
set noexpandtab

" Ignore build artifacts
set wildignore+=*.exe,bin/*,release/*,.go/*,site/*,*.tar.gz

" Error format for Go
set errorformat=%f:%l:%c:\ %m,%f:%l:\ %m

" =====================================
" Project Keybindings (<leader>p)
" =====================================

" Build commands
nnoremap <leader>pbb :!make build<CR>
nnoremap <leader>pbs :!make static<CR>
nnoremap <leader>pba :!make build-all<CR>
nnoremap <leader>pbc :!make clean<CR>

" Run commands
nnoremap <leader>prr :!make dev<CR>
nnoremap <leader>prn :!nix run<CR>

" Test commands
nnoremap <leader>ptt :!make test<CR>
nnoremap <leader>ptc :!make check<CR>
nnoremap <leader>ptf :execute '!go test -v -run ' . expand('<cword>') . ' ./...'<CR>
nnoremap <leader>ptp :execute '!go test -v ' . expand('%:h')<CR>

" Lint/Format commands
nnoremap <leader>pll :!make lint<CR>
nnoremap <leader>plf :!make fmt<CR>

" Dependencies
nnoremap <leader>pdd :!make deps<CR>
nnoremap <leader>pdm :!go mod tidy<CR>

" Documentation
nnoremap <leader>pos :!mkdocs serve &<CR>
nnoremap <leader>pob :!mkdocs build<CR>
nnoremap <leader>poo :!xdg-open http://localhost:8000 &<CR>

" Release commands
nnoremap <leader>pxr :!make release<CR>
nnoremap <leader>pxc :!make release-clean<CR>
nnoremap <leader>pxn :!nix run .#release<CR>

" Package commands
nnoremap <leader>pkd :!make deb<CR>
nnoremap <leader>pkr :!make rpm<CR>
nnoremap <leader>pks :!make snap<CR>
nnoremap <leader>pkf :!make flatpak<CR>
nnoremap <leader>pka :!make packages<CR>

" Nix commands
nnoremap <leader>pnd :terminal nix develop<CR>
nnoremap <leader>pnb :!nix build<CR>
nnoremap <leader>pnf :!nix flake check<CR>
nnoremap <leader>pnu :!nix flake update<CR>
nnoremap <leader>pns :!nix flake show<CR>

" Git/GitHub commands
nnoremap <leader>pgs :!git status<CR>
nnoremap <leader>pgp :!gh pr list<CR>
nnoremap <leader>pgi :!gh issue list<CR>
nnoremap <leader>pgr :!gh release list<CR>

" Debug commands (Delve)
nnoremap <leader>pDd :terminal dlv debug .<CR>
nnoremap <leader>pDs :terminal dlv debug . -- systray<CR>
nnoremap <leader>pDt :terminal dlv test ./...<CR>
nnoremap <leader>pDf :execute 'terminal dlv test ' . expand('%:h') . ' -- -test.run ' . expand('<cword>')<CR>
nnoremap <leader>pDp :execute 'terminal dlv test ' . expand('%:h')<CR>
nnoremap <leader>pDh :terminal dlv debug . --headless --listen=:2345 --api-version=2<CR>
nnoremap <leader>pDc :terminal dlv connect 127.0.0.1:2345<CR>

" Info commands
nnoremap <leader>pii :!make info<CR>
nnoremap <leader>pih :!make help<CR>
nnoremap <leader>pic :!go version && echo '' && go env<CR>

" =====================================
" Quick Reference
" =====================================
" <leader>pb* - Build (build, static, all, clean)
" <leader>pr* - Run (run, nix)
" <leader>pt* - Test (test, check, function, package)
" <leader>pl* - Lint (lint, format)
" <leader>pd* - Dependencies
" <leader>pD* - Debug (debug, systray, tests, attach, headless)
" <leader>po* - Documentation
" <leader>px* - Release
" <leader>pk* - Packages
" <leader>pn* - Nix
" <leader>pg* - Git/GitHub
" <leader>pi* - Info
"
" Debug Keybindings (with nvim-dap):
" <leader>pDb - Toggle breakpoint
" <leader>pDB - Conditional breakpoint
" <leader>pDr - Start/Continue
" <leader>pDn - Step over
" <leader>pDi - Step into
" <leader>pDo - Step out
" <leader>pDq - Stop
" <leader>pDu - Toggle DAP UI
