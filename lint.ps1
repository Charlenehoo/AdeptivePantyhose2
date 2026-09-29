param(
    [string]$Preset = "debug",
    [string]$Target = "src/",
    [switch]$Fix
)

$runClangTidy = "C:\Users\CharleneHoo\Scoop\apps\llvm\current\bin\run-clang-tidy"

$clangTidyArgs = @(
    "-p", "build/$Preset",
    "-extra-arg=-fms-extensions",
    "-extra-arg=-fms-compatibility",
    "-extra-arg=-Wno-unknown-pragmas",
    "-extra-arg=-Wno-unknown-attributes",
    "-extra-arg=-Wno-ignored-attributes",
    "-extra-arg=-D_ITERATOR_DEBUG_LEVEL=0",
    "-header-filter=.*[\\/]src[\\/].*"
)

if ($Fix) { $clangTidyArgs += "-fix" }
$clangTidyArgs += $Target

Write-Host "run-clang-tidy $($clangTidyArgs -join ' ')" -ForegroundColor Cyan
& python $runClangTidy @clangTidyArgs