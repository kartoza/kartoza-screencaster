" Kartoza Screencaster - Vim/Neovim Project Settings (C++ / Qt6)
" ===============================================================

" C++ indent: 2 spaces, no tabs
set tabstop=2
set shiftwidth=2
set expandtab
set cinoptions=g0,N-s,i2s,+2s,(0,w1,W2s

" Error format for GCC/clang
set errorformat=%f:%l:%c:\ %m,%f:%l:\ %m

" Ignore build artifacts
set wildignore+=build/*,*.o,*.moc,*_autogen/*,*.so,*.a

" =====================================
" Project Keybindings (<leader>p)
" =====================================

" Build — all via ksc-dev (single build path: timing + build-log.tsv + ccache
" stats -> cmake+ninja -> CMakeLists.txt ccache/mold). Edit ksc-dev.sh only.
nnoremap <leader>pbb :!ksc-dev build<CR>
nnoremap <leader>pbr :!ksc-dev release<CR>
nnoremap <leader>pbc :!ksc-dev clean<CR>
nnoremap <leader>pbs :!ksc-dev stats --graph<CR>

" Run
nnoremap <leader>prr :!ksc-dev run<CR>

" Test
nnoremap <leader>ptt :!ksc-dev test<CR>
nnoremap <leader>ptm :!ksc-dev test -R test_merger<CR>
nnoremap <leader>ptp :!ksc-dev test -R test_pipeline<CR>

" Lint/Format
nnoremap <leader>plf :!ksc-dev format<CR>
nnoremap <leader>plt :!cd build && run-clang-tidy -p . ../src/<CR>

" Debug
nnoremap <leader>pDd :terminal gdb -q ./build/kartoza-screencaster<CR>

" Profile
nnoremap <leader>pPv :terminal valgrind --leak-check=full ./build/kartoza-screencaster<CR>

" Docs
nnoremap <leader>pod :!doxygen Doxyfile && xdg-open docs/doxygen/html/index.html<CR>
nnoremap <leader>pos :!mkdocs serve &<CR>

" Info
nnoremap <leader>pii :!cmake --version && ninja --version && ccache -s<CR>
nnoremap <leader>pis :!wc -l src/**/*.cpp src/**/*.h \| sort -rn \| head -20<CR>

" =====================================
" Quick Reference (<leader>p)
" =====================================
" pb* - Build (build, release, clean)
" pr* - Run
" pt* - Test (all, merger, pipeline)
" pl* - Lint/Format (clang-format, clang-tidy)
" pD* - Debug (GDB, attach)
" pP* - Profile (valgrind, callgrind, cachegrind, massif)
" po* - Docs (doxygen, mkdocs)
" pn* - Nix
" pg* - Git/GitHub
" pi* - Info
