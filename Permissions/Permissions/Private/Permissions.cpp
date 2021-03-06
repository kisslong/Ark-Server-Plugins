#include "../Public/Permissions.h"

#include "../Public/DBHelper.h"
#include "Main.h"

namespace Permissions
{
	TArray<FString> GetPlayerGroups(uint64 steam_id)
	{
		TArray<FString> groups;

		try
		{
			auto& db = GetDB();

			std::string groups_str;
			db << "SELECT Groups FROM Players WHERE SteamId = ?;" << steam_id >> groups_str;

			FString groups_fstr(groups_str.c_str());

			groups_fstr.ParseIntoArray(groups, L",", true);
		}
		catch (const sqlite::sqlite_exception& exception)
		{
			Log::GetLog()->error("({} {}) Unexpected DB error {}", __FILE__, __FUNCTION__, exception.what());
		}

		return groups;
	}

	TArray<FString> GetGroupPermissions(const FString& group)
	{
		if (group.IsEmpty())
			return {};

		TArray<FString> permissions;

		try
		{
			auto& db = GetDB();

			std::string permissions_str;
			db << "SELECT Permissions FROM Groups WHERE GroupName = ?;" << group.ToString() >> permissions_str;

			FString permissions_fstr(permissions_str.c_str());

			permissions_fstr.ParseIntoArray(permissions, L",", true);
		}
		catch (const sqlite::sqlite_exception& exception)
		{
			Log::GetLog()->error("({} {}) Unexpected DB error {}", __FILE__, __FUNCTION__, exception.what());
		}

		return permissions;
	}

	TArray<uint64> GetGroupMembers(const FString& group)
	{
		TArray<uint64> members;

		try
		{
			auto& db = GetDB();

			auto res = db << "SELECT SteamId FROM Players;";
			res >> [&members, &group](uint64 steam_id)
			{
				if (IsPlayerInGroup(steam_id, group))
					members.Add(steam_id);
			};
		}
		catch (const sqlite::sqlite_exception& exception)
		{
			Log::GetLog()->error("({} {}) Unexpected DB error {}", __FILE__, __FUNCTION__, exception.what());
		}

		return members;
	}

	bool IsPlayerInGroup(uint64 steam_id, const FString& group)
	{
		TArray<FString> groups = GetPlayerGroups(steam_id);

		for (const auto& current_group : groups)
		{
			if (current_group == group)
				return true;
		}

		return false;
	}

	std::optional<std::string> AddPlayerToGroup(uint64 steam_id, const FString& group)
	{
		if (!DB::IsPlayerExists(steam_id) || !DB::IsGroupExists(group))
			return "Player or group does not exist";

		if (IsPlayerInGroup(steam_id, group))
			return "Player was already added";

		try
		{
			auto& db = GetDB();

			db << "UPDATE Players SET Groups = Groups || ? || ',' WHERE SteamId = ?;" << group.ToString() << steam_id;
		}
		catch (const sqlite::sqlite_exception& exception)
		{
			Log::GetLog()->error("({} {}) Unexpected DB error {}", __FILE__, __FUNCTION__, exception.what());
			return "Unexpected DB error";
		}

		return {};
	}

	std::optional<std::string> RemovePlayerFromGroup(uint64 steam_id, const FString& group)
	{
		if (!DB::IsPlayerExists(steam_id) || !DB::IsGroupExists(group))
			return "Player or group does not exist";

		if (!IsPlayerInGroup(steam_id, group))
			return "Player is not in group";

		TArray<FString> groups = GetPlayerGroups(steam_id);

		FString new_groups;

		for (const FString& current_group : groups)
		{
			if (current_group != group)
				new_groups += current_group + ",";
		}

		try
		{
			auto& db = GetDB();

			db << "UPDATE Players SET Groups = ? WHERE SteamId = ?;" << new_groups.ToString() << steam_id;
		}
		catch (const sqlite::sqlite_exception& exception)
		{
			Log::GetLog()->error("({} {}) Unexpected DB error {}", __FILE__, __FUNCTION__, exception.what());
			return "Unexpected DB error";
		}

		return {};
	}

	std::optional<std::string> AddGroup(const FString& group)
	{
		if (DB::IsGroupExists(group))
			return "Group already exists";

		try
		{
			auto& db = GetDB();

			db << "INSERT INTO Groups (GroupName) VALUES (?);"
				<< group.ToString();
		}
		catch (const sqlite::sqlite_exception& exception)
		{
			Log::GetLog()->error("({} {}) Unexpected DB error {}", __FILE__, __FUNCTION__, exception.what());
			return "Unexpected DB error";
		}

		return {};
	}

	std::optional<std::string> RemoveGroup(const FString& group)
	{
		if (!DB::IsGroupExists(group))
			return "Group does not exist";

		// Remove all players from this group

		TArray<uint64> group_members = GetGroupMembers(group);
		for (uint64 player : group_members)
		{
			RemovePlayerFromGroup(player, group);
		}

		// Delete group

		try
		{
			auto& db = GetDB();

			db << "DELETE FROM Groups WHERE GroupName = ?;" << group.ToString();
		}
		catch (const sqlite::sqlite_exception& exception)
		{
			Log::GetLog()->error("({} {}) Unexpected DB error {}", __FILE__, __FUNCTION__, exception.what());
			return "Unexpected DB error";
		}

		return {};
	}

	bool IsGroupHasPermission(const FString& group, const FString& permission)
	{
		if (!DB::IsGroupExists(group))
			return false;

		TArray<FString> permissions = GetGroupPermissions(group);

		for (const auto& current_perm : permissions)
		{
			if (current_perm == permission)
				return true;
		}

		return false;
	}

	bool IsPlayerHasPermission(uint64 steam_id, const FString& permission)
	{
		TArray<FString> groups = GetPlayerGroups(steam_id);

		for (const auto& current_group : groups)
		{
			if (IsGroupHasPermission(current_group, permission) || IsGroupHasPermission(current_group, "*"))
				return true;
		}

		return false;
	}

	std::optional<std::string> GroupGrantPermission(const FString& group, const FString& permission)
	{
		if (!DB::IsGroupExists(group))
			return "Group does not exist";

		if (IsGroupHasPermission(group, permission))
			return "Group already has this permission";

		try
		{
			auto& db = GetDB();

			db << "UPDATE Groups SET Permissions = Permissions || ? || ',' WHERE GroupName = ?;" << permission.ToString() <<
				group.ToString();
		}
		catch (const sqlite::sqlite_exception& exception)
		{
			Log::GetLog()->error("({} {}) Unexpected DB error {}", __FILE__, __FUNCTION__, exception.what());
			return "Unexpected DB error";
		}

		return {};
	}

	std::optional<std::string> GroupRevokePermission(const FString& group, const FString& permission)
	{
		if (!DB::IsGroupExists(group))
			return "Group does not exist";

		if (!IsGroupHasPermission(group, permission))
			return "Group does not have this permission";

		TArray<FString> permissions = GetGroupPermissions(group);

		FString new_permissions;

		for (const FString& current_perm : permissions)
		{
			if (current_perm != permission)
				new_permissions += current_perm + ",";
		}

		try
		{
			auto& db = GetDB();

			db << "UPDATE Groups SET Permissions = ? WHERE GroupName = ?;" << new_permissions.ToString() << group.ToString();
		}
		catch (const sqlite::sqlite_exception& exception)
		{
			Log::GetLog()->error("({} {}) Unexpected DB error {}", __FILE__, __FUNCTION__, exception.what());
			return "Unexpected DB error";
		}

		return {};
	}
}
