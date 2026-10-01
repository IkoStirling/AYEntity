# Actor class inheritance example

`Assets/actors/EnemyActor.ayactor` defines a reusable ECS component and
property template. `EliteEnemy.ayactor` inherits it, changes two properties
and patches the health component's `maxHp`. Both use `EnemyActor.logia` because
the child leaves `script` empty.

In the editor, create an Actor class under `Assets`, then create a child from
the selected class. Open the class data in the code window to edit defaults,
and drag either class into a Scene to create an instance. Use the Inspector's
restore defaults action to discard that instance's overrides. Its Scene
history records the operation for undo and redo.

`removeComponents` and `removeProperties` remove inherited defaults. A child
with its own `script` replaces the inherited script; Logia has no `super`
dispatch. The runtime stores instances as ordinary ECS Entities with an
`ActorInstanceComponent`, so systems continue to read normal components.

The editor can reload saved Logia behavior during Play when the script watcher
is enabled. Class structure and default changes take effect when a Scene is
loaded or Play restarts; existing Play Entities are not migrated in place.
Schema 1 class files remain readable; new saves use schema 2. Actor class and
script files are loose assets under the same resource root as the Scene.
Actor `on_update` runs in the Gameplay phase, so it should not be used as a
deterministic fixed-step authority or replication protocol. Networked games
should put authoritative state in the existing ECS/network systems and treat
the Actor class as an authoring and behavior facade.
