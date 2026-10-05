#include <doctest/doctest.h>

#include "Starfall/ECS/Registry.h"

using namespace Starfall;

namespace {
	struct Position { float X = 0, Y = 0; };
	struct Velocity { float X = 0, Y = 0; };
	struct Tag { int Value = 0; Tag() = default; explicit Tag(int v) : Value(v) {} };
}

TEST_CASE("Registry create/destroy/valid")
{
	Registry r;
	EntityHandle a = r.Create();
	EntityHandle b = r.Create();
	CHECK(a != b);
	CHECK(r.Valid(a));
	CHECK(r.AliveCount() == 2);
	r.Destroy(a);
	CHECK_FALSE(r.Valid(a));
	CHECK(r.Valid(b));
	CHECK(r.AliveCount() == 1);
	EntityHandle c = r.Create(); // recycles index with a new generation
	CHECK(c != a);
	CHECK_FALSE(r.Valid(a));
	CHECK(r.Valid(c));
	CHECK_FALSE(r.Valid(EntityHandle::Null));
}

TEST_CASE("Registry components")
{
	Registry r;
	EntityHandle e = r.Create();
	CHECK_FALSE(r.Has<Position>(e));
	r.Add<Position>(e, Position{ 1, 2 });
	CHECK(r.Has<Position>(e));
	CHECK(r.Get<Position>(e).Y == 2);
	CHECK(r.TryGet<Velocity>(e) == nullptr);
	r.AddOrReplace<Tag>(e, 5);
	r.AddOrReplace<Tag>(e, 7);
	CHECK(r.Get<Tag>(e).Value == 7);
	CHECK((r.Has<Position, Tag>(e)));
	r.Remove<Position>(e);
	CHECK_FALSE(r.Has<Position>(e));
	r.Destroy(e);
	CHECK(r.TryGet<Tag>(e) == nullptr);
}

TEST_CASE("Registry swap-remove keeps other components intact")
{
	Registry r;
	std::vector<EntityHandle> es;
	for(int i = 0; i < 10; i++)
	{
		es.push_back(r.Create());
		r.Add<Tag>(es.back(), i);
	}
	r.Destroy(es[3]);
	r.Destroy(es[0]);
	for(int i = 0; i < 10; i++)
	{
		if(i == 3 || i == 0)
			continue;
		CHECK(r.Get<Tag>(es[i]).Value == i);
	}
}

TEST_CASE("Registry Each multi component and destroy during iteration")
{
	Registry r;
	int count = 0;
	for(int i = 0; i < 20; i++)
	{
		EntityHandle e = r.Create();
		r.Add<Position>(e);
		if(i % 2 == 0)
			r.Add<Velocity>(e, Velocity{ 1, 0 });
	}
	r.Each<Position, Velocity>([&](EntityHandle, Position& p, Velocity& v) { p.X += v.X; count++; });
	CHECK(count == 10);
	r.Each<Position>([&](EntityHandle e, Position&) { r.Destroy(e); });
	CHECK(r.AliveCount() == 0);
	int none = 0;
	r.Each<Velocity>([&](EntityHandle, Velocity&) { none++; });
	CHECK(none == 0);
}

TEST_CASE("Registry EachEntity and Clear")
{
	Registry r;
	for(int i = 0; i < 5; i++)
		r.Create();
	int n = 0;
	r.EachEntity([&](EntityHandle) { n++; });
	CHECK(n == 5);
	r.Clear();
	CHECK(r.AliveCount() == 0);
}
