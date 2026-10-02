$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
function Set-PinnedReplacement([string]$Relative,[string]$Before,[string]$After){
    $file=Join-Path $project ('third_party\RE-UE4SS\'+$Relative)
    $text=Get-Content -LiteralPath $file -Raw
    if($text.Contains($After) -and ($After.Contains($Before) -or -not $text.Contains($Before))){return}
    if(-not $text.Contains($Before)){throw "Pinned upstream context changed: $Relative"}
    [IO.File]::WriteAllText($file,$text.Replace($Before,$After),[Text.UTF8Encoding]::new($false))
}
$initializer='deps\first\Unreal\src\UnrealInitializer.cpp'
Set-PinnedReplacement $initializer 'i < 2000 &&' 'i < 1 &&'
$marker='            if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count() > UnrealConfig.SecondsToScanBeforeGivingUp)'
Set-PinnedReplacement $initializer $marker ('            throw std::runtime_error{"Briefcase: one-shot signature scan failed"};'+[Environment]::NewLine+$marker)
Set-PinnedReplacement $initializer '        HookProcessConsoleExec();' '        // Briefcase minimal backend: console hooks disabled.'
Set-PinnedReplacement $initializer '        HookUStructLink();' '        // Briefcase minimal backend: struct-link hook disabled.'
Set-PinnedReplacement $initializer '        HookStaticConstructObject();' '        // Briefcase minimal backend: mod-construction hook disabled.'
Set-PinnedReplacement $initializer '        if (UnrealConfig.bHookLoadMap) { HookLoadMap(); }' '        // Briefcase dedicated server: the UE4SS 3.0.1 LoadMap detour is incompatible with the current game build.'
Set-PinnedReplacement 'deps\first\SinglePassSigScanner\src\SinglePassSigScanner.cpp' '(byte*)' '(uint8_t*)'
Set-PinnedReplacement 'deps\first\Unreal\include\Unreal\VirtualFunctionHelper.hpp' 'DispatchMap.template find<ObjectClassType>(ObjectClass)' 'DispatchMap.find(ObjectClass)'
Set-PinnedReplacement 'deps\first\Unreal\include\Unreal\ULocalPlayer.hpp' '    // TODO: Move to its own file.' ('    enum EAspectRatioAxisConstraint : int;'+[Environment]::NewLine+'    // TODO: Move to its own file.')

# UE4SS 3.0.1 originally followed ImGuiColorTextEdit master. The current
# branch targets newer ImGui APIs and no longer builds with UE4SS's ImGui 1.89.
Set-PinnedReplacement 'deps\third\CMakeLists.txt' '    GIT_TAG master' '    GIT_TAG af7821926251feca84e35f8fa83eee84dae90424'
# A shallow clone of the default branch does not contain this pinned commit.
# Fetch its history so a clean release runner can check out the exact revision.
$thirdParty = Join-Path $project 'third_party\RE-UE4SS\deps\third\CMakeLists.txt'
$thirdPartyText = Get-Content -LiteralPath $thirdParty -Raw
$shallowPattern = '(GIT_TAG af7821926251feca84e35f8fa83eee84dae90424\r?\n[ \t]*GIT_SHALLOW )TRUE'
if ([regex]::Matches($thirdPartyText, $shallowPattern).Count -eq 1) {
    $thirdPartyText = [regex]::Replace($thirdPartyText, $shallowPattern, '${1}FALSE')
    [IO.File]::WriteAllText($thirdParty, $thirdPartyText, [Text.UTF8Encoding]::new($false))
} elseif ($thirdPartyText -notmatch 'GIT_TAG af7821926251feca84e35f8fa83eee84dae90424\r?\n[ \t]*GIT_SHALLOW FALSE') {
    throw 'Pinned ImGuiColorTextEdit checkout policy changed.'
}
foreach($repository in @('ocornut/imgui','UE4SS-RE/ImGuiColorTextEdit','juliettef/IconFontCppHeaders','zyantific/zydis',
                          'stevemk14ebr/PolyHook_2_0','MolecularMatters/raw_pdb')) {
    Set-PinnedReplacement 'deps\third\CMakeLists.txt' "git@github.com:$repository.git" "https://github.com/$repository.git"
}
Set-PinnedReplacement 'deps\first\Profiler\CMakeLists.txt' 'git@github.com:wolfpld/tracy.git' 'https://github.com/wolfpld/tracy.git'

# Keep the Win64 root small. Briefcase packages UE4SS, settings and mods below
# Win64/ue4ss. The server launcher loads UE4SS.dll through its injection bootstrap.
Set-PinnedReplacement 'UE4SS\proxy_generator\main.cpp' 'LoadLibrary(STR(\"UE4SS.dll\"))' 'LoadLibrary(STR(\"ue4ss\\\\UE4SS.dll\"))'
