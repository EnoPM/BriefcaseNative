# API C++ typée : Spy

Inclure <Briefcase/DeceiveInc/Spy.hpp> et lier la cible CMake Briefcase.DeceiveInc.
Cette bibliothèque fait partie du SDK Briefcase. Elle est compilée dans le mod,
sans nouvelle DLL à installer et sans dépendance à UE4SS ou ImGui dans l'API publique.
L'ABI C existante reste le seul contrat entre le mod et le runtime.

## Utilisation

Ce code s'exécute dans une tâche du game thread, après initialisation d'Unreal :

```cpp
using briefcase::deceive_inc::SpyApi;

SpyApi spies(host); // À conserver pour toute la durée d'utilisation du mod.

for (auto& spy : spies.FindAll()) {
    if (spy.IsDead())
        continue;

    bool isBot = spy.IsBot();
    auto position = spy.GetLocation();
    auto view = spy.GetEyesViewPoint();
}
```

Pour un développeur C#, Spy est comparable à un objet qui encapsule un accès natif.
SpyApi centralise sa création et les fonctions qu'il utilise.
L'équivalent de la réflexion et des conversions est caché dans Briefcase.
auto laisse le compilateur déduire le type, comme var en C#.

## Opérations

- IsDead, IsBot, IsLocallyControlled, IsInADS : booléens lus dans le jeu.
- GetLocation, GetVelocity : Vector3 (x, y, z), en centimètres et centimètres/seconde.
- GetEyesViewPoint : position et rotation (pitch, yaw, roll en degrés).
- GetObjectPath : chemin Unreal de l'instance, sans interprétation du nom d'agent.
- GetController, GetWeaponTool : références ObjectHandle possédées, éventuellement vides.
- IsValid : indique si la référence est encore valide au moment de l'appel.
- Handle : référence empruntée pour les opérations génériques de l'ABI.
- Reset : libération anticipée et idempotente.

FindAll() recherche les Spy vivants au sens UObject, y compris les personnages morts.
FindAll(localSpy.Handle()) limite la recherche au monde de ce personnage.
La limite du backend reste de 512 objets : un dépassement produit une erreur,
pas une liste silencieusement tronquée.
FromHandle(handle) vérifie le type réel puis retient une référence empruntée
appartenant au même mod. Les références d'un autre mod sont refusées par le runtime.

## Durée de vie et erreurs

Spy et ObjectHandle libèrent automatiquement leurs références à la sortie de leur
portée, comme un IDisposable utilisé avec using en C#. Leurs copies sont interdites ;
std::move transfère la référence et laisse la source vide. Conserver une référence
n'empêche pas Unreal de détruire le personnage. IsValid ne remplace pas la gestion
des erreurs d'invocation lorsqu'un objet disparaît.

Chaque Spy conserve les fonctions de sa session en vie. Il faut donc vider les
collections et références conservées, puis libérer SpyApi dans BriefcaseModUnload.
Toutes ces opérations, y compris les destructeurs, s'exécutent sur le game thread.
Aucun cache global ne mélange les propriétaires de mods.

Capacités utilisées : unreal.reflection, unreal.invoke et game-thread pour planifier
les tâches. La couche typée conserve les contrôles de l'ABI et du backend.

