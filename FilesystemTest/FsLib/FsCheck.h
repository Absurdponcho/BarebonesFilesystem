#pragma once

class CheckImplementer
{
public:
	static void Check(const char* Message);
};

#define fsCheck(Condition, Message) if (!(Condition)) { CheckImplementer::Check(Message); }
