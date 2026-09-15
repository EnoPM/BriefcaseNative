#pragma once
#include <array>
namespace briefcase::deceive_inc::detail {
// Pinned to DeceiveInc.Client.6A96564B-06283000; resolve_function validates the live build.
enum class SpyFunction { Dead, Bot, Local, Controller, Location, Velocity, Eyes, ADS, Weapon, Count };
struct SpyContract {
    const char *path;
    const char *signature;
};
inline constexpr std::array<SpyContract, 9> spy_contracts{
    {{"/Script/DeceiveInc.Spy:IsDead",
      R"CONTRACT({"parameterSize":1,"flags":1409418241,"parameters":[{"name":"ReturnValue","type":"bool","offset":0,"return":true,"output":false}]})CONTRACT"},
     {"/Script/DeceiveInc.Spy:IsBot",
      R"CONTRACT({"parameterSize":1,"flags":1409418241,"parameters":[{"name":"ReturnValue","type":"bool","offset":0,"return":true,"output":false}]})CONTRACT"},
     {"/Script/Engine.Pawn:IsLocallyControlled",
      R"CONTRACT({"parameterSize":1,"flags":1409418240,"parameters":[{"name":"ReturnValue","type":"bool","offset":0,"return":true,"output":false}]})CONTRACT"},
     {"/Script/Engine.Pawn:GetController",
      R"CONTRACT({"parameterSize":8,"flags":1409418241,"parameters":[{"name":"ReturnValue","type":"object","offset":0,"return":true,"output":false}]})CONTRACT"},
     {"/Script/Engine.Actor:K2_GetActorLocation",
      R"CONTRACT({"parameterSize":12,"flags":1417806849,"parameters":[{"name":"ReturnValue","type":"vector","offset":0,"return":true,"output":false}]})CONTRACT"},
     {"/Script/Engine.Actor:GetVelocity",
      R"CONTRACT({"parameterSize":12,"flags":1417806848,"parameters":[{"name":"ReturnValue","type":"vector","offset":0,"return":true,"output":false}]})CONTRACT"},
     {"/Script/Engine.Actor:GetActorEyesViewPoint",
      R"CONTRACT({"parameterSize":24,"flags":1422001152,"parameters":[{"name":"OutLocation","type":"vector","offset":0,"return":false,"output":true},{"name":"OutRotation","type":"rotator","offset":12,"return":false,"output":true}]})CONTRACT"},
     {"/Script/DeceiveInc.Spy:IsInADS",
      R"CONTRACT({"parameterSize":1,"flags":1409418241,"parameters":[{"name":"ReturnValue","type":"bool","offset":0,"return":true,"output":false}]})CONTRACT"},
     {"/Script/DeceiveInc.Spy:GetWeaponTool",
      R"CONTRACT({"parameterSize":8,"flags":1409418241,"parameters":[{"name":"ReturnValue","type":"object","offset":0,"return":true,"output":false}]})CONTRACT"}}};
} // namespace briefcase::deceive_inc::detail
