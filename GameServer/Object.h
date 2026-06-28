#pragma once


class Object : public enable_shared_from_this<Object>
{
public:
	Object();
	virtual ~Object();

	bool IsPlayer() { return _isPlayer; }

public:
	unique_ptr<Protocol::ObjectInfo> objectInfo;
	Protocol::Vec2Fixed* position = nullptr;

public:
	atomic<weak_ptr<Room>> room;

protected:
	bool _isPlayer = false;
};

