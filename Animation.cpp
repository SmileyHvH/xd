#include "Animation.hpp"
#include "../helpers/Math.hpp"


void Animation::UpdateAnimationAngles(AnimationInfo &anim, QAngle angles)
{
	return;
	C_BasePlayer::UpdateAnimationState(anim.m_playerAnimState, angles);
}

AnimationInfo &Animation::GetPlayerAnimationInfo(int32_t idx)
{
	return arr_infos[idx];
}

AnimationInfo &Animation::GetPlayerAnimationInfo(int32_t idx, float time)
{
	for (auto i = arr_infos_record[idx].begin(); i != arr_infos_record[idx].end(); i++)
	{
		if (time == i->first)
			return i->second;
	}
	return AnimationInfo();
}

std::deque<std::pair<float, AnimationInfo>> Animation::GetPlayerAnimationRecord(int32_t idx)
{
	return arr_infos_record[idx];
}

void Animation::RestoreAnim(C_BasePlayer *player, float time)
{
	return;
	auto anim = GetPlayerAnimationInfo(player->EntIndex(), time);
	if (anim.m_flSpawnTime > 0.1f)
		ApplyAnim(player, anim);
}

void Animation::RestoreAnim(C_BasePlayer *player)
{
	return;
	if (arr_infos_record[player->EntIndex()].empty())
		return;

	ApplyAnim(player, arr_infos_record[player->EntIndex()].front().second);
}

void Animation::ApplyAnim(C_BasePlayer *player, AnimationInfo anim)
{
	return;
	player->m_flPoseParameter() = anim.m_flPoseParameters;
	std::memcpy(player->GetAnimOverlays(), anim.m_AnimationLayer, (sizeof(AnimationLayer) * player->GetNumAnimOverlays()));
}
