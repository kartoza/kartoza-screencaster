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

" Build
nnoremap <leader>pbb :!cd build && ninja<CR>
nnoremap <leader>pbr :!cd build && cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release && ninja<CR>
nnoremap <leader>pbc :!cd build && ninja clean<CR>

" Run
nnoremap <leader>prr :!./build/kartoza-screencaster<CR>

" Test
nnoremap <leader>ptt :!cd build && ctest --output-on-failure<CR>
nnoremap <leader>ptm :!cd build && ./test_merger -v1<CR>
nnoremap <leader>ptp :!cd build && ./test_pipeline -v1<CR>

" Lint/Format
nnoremap <leader>plf :!find src tests -name '*.cpp' -o -name '*.h' \| xargs clang-format -i<CR>
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
