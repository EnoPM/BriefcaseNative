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
Set-PinnedReplacement 'deps\first\SinglePassSigScanner\src\SinglePassSigScanner.cpp' '(byte*)' '(uint8_t*)'
Set-PinnedReplacement 'deps\first\Unreal\include\Unreal\VirtualFunctionHelper.hpp' 'DispatchMap.template find<ObjectClassType>(ObjectClass)' 'DispatchMap.find(ObjectClass)'
Set-PinnedReplacement 'deps\first\Unreal\include\Unreal\ULocalPlayer.hpp' '    // TODO: Move to its own file.' ('    enum EAspectRatioAxisConstraint : int;'+[Environment]::NewLine+'    // TODO: Move to its own file.')
