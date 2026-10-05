#include <doctest/doctest.h>

#include "Starfall/Scene/Entity.h"
#include "Starfall/Scene/SceneHistory.h"
#include "Starfall/Scene/SceneSerializer.h"

using namespace Starfall;

TEST_CASE("SceneHistory undo/redo semantics")
{
	SceneHistory history(5);
	CHECK_FALSE(history.CanUndo());
	CHECK(history.Undo() == nullptr);
	history.Reset("A");
	CHECK_FALSE(history.Push("A", "noop"));  // identical state is ignored
	CHECK(history.Push("B", "edit B"));
	CHECK(history.Push("C", "edit C"));
	CHECK(history.GetUndoLabel() == "edit C");
	CHECK(*history.Undo() == "B");
	CHECK(history.GetRedoLabel() == "edit C");
	CHECK(*history.Undo() == "A");
	CHECK_FALSE(history.CanUndo());
	CHECK(*history.Redo() == "B");
	CHECK(history.Push("D", "edit D")); // drops the redo branch
	CHECK_FALSE(history.CanRedo());
	CHECK(*history.Undo() == "B");

	// bounded size
	history.Reset("0");
	for(int i = 1; i <= 20; i++)
		history.Push(std::to_string(i), "e");
	CHECK(history.GetCount() == 5);
	int undos = 0;
	while(history.Undo())
		undos++;
	CHECK(undos == 4);
}

TEST_CASE("SceneHistory restores real scenes")
{
	Scene scene;
	Entity e = scene.CreateEntity("One");
	SceneHistory history;
	history.Reset(SceneSerializer::SerializeToString(scene));
	UUID id = e.GetUUID();

	e.Transform().Translation = { 5, 0, 0 };
	history.Push(SceneSerializer::SerializeToString(scene), "move");
	scene.CreateEntity("Two");
	history.Push(SceneSerializer::SerializeToString(scene), "create");

	REQUIRE(SceneSerializer::DeserializeFromString(scene, *history.Undo()));
	CHECK(scene.GetEntityCount() == 1);
	CHECK(scene.FindEntityByUUID(id).Transform().Translation.x == 5.0f);
	REQUIRE(SceneSerializer::DeserializeFromString(scene, *history.Undo()));
	CHECK(scene.FindEntityByUUID(id).Transform().Translation.x == 0.0f);
	REQUIRE(SceneSerializer::DeserializeFromString(scene, *history.Redo()));
	REQUIRE(SceneSerializer::DeserializeFromString(scene, *history.Redo()));
	CHECK(scene.GetEntityCount() == 2);
}
