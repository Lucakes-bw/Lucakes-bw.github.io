// 《观察者模式》硬编码版：播放状态
#pragma once
enum class PlayState { Stopped, Playing, Paused };
inline const char* name(PlayState s) { return s == PlayState::Playing ? "播放" : s == PlayState::Paused ? "暂停" : "停止"; }
