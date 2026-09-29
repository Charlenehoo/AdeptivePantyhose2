param(
    [string]$Filter = "src[/\\]",
    [switch]$Fix
)

$runClangTidy = "C:\Users\CharleneHoo\Scoop\apps\llvm\current\bin\run-clang-tidy"

$clangTidyArgs = @(
    "-p", "build/clang-tidy",                              # ← 固定指向
    "-extra-arg=-fms-extensions",
    "-extra-arg=-fms-compatibility",
    "-extra-arg=-Wno-unknown-pragmas",
    "-extra-arg=-Wno-unknown-attributes",
    "-extra-arg=-Wno-ignored-attributes",
    "-extra-arg=-Wno-unused-command-line-argument",
    "-extra-arg=-D_ITERATOR_DEBUG_LEVEL=0",
    "-header-filter=.*[\\/]src[\\/].*"
)

if ($Fix) { $clangTidyArgs += "-fix" }
$clangTidyArgs += $Filter

& python $runClangTidy @clangTidyArgs