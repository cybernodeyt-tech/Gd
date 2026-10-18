// Autopsy Bot - personal frame-based record/replay for studying levels.
// v0.1: record a run, save it per level, replay it from the pause menu.
// Playback is blocked unless practice mode is on or your safe-mode mod is loaded.
// NOTE: hook signatures (handleButton, processCommands) must match the current
// Geode bindings. If the build complains, check them in geode-sdk/bindings.

#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace geode::prelude;

enum class Mode { Idle, Record, Play };

struct Input {
	int frame;
	int button;
	bool down;
	bool player1;
};

namespace Bot {
	inline Mode mode = Mode::Idle;
	inline int frame = 0;
	inline size_t cursor = 0;
	inline bool injecting = false;
	inline std::vector<Input> inputs;

	inline int currentLevelID() {
		auto pl = PlayLayer::get();
		if (!pl || !pl->m_level) return 0;
		return pl->m_level->m_levelID.value();
	}

	inline std::filesystem::path pathFor(int levelID) {
		return Mod::get()->getSaveDir() / fmt::format("replay_{}.txt", levelID);
	}

	inline void save(int levelID) {
		std::ofstream out(pathFor(levelID));
		for (auto& i : inputs) {
			out << i.frame << ' ' << i.button << ' ' << (i.down ? 1 : 0) << ' ' << (i.player1 ? 1 : 0) << '\n';
		}
	}

	inline bool load(int levelID) {
		std::ifstream in(pathFor(levelID));
		if (!in) return false;
		inputs.clear();
		int f, b, d, p;
		while (in >> f >> b >> d >> p) {
			inputs.push_back({f, b, d != 0, p != 0});
		}
		return !inputs.empty();
	}

	// Safety guard: only play back when no progress can be recorded.
	inline bool playbackAllowed() {
		auto id = Mod::get()->getSettingValue<std::string>("safe-mod-id");
		if (!id.empty()) {
			auto safeMod = Loader::get()->getLoadedMod(id);
			if (safeMod && safeMod->isEnabled()) return true;
		}
		auto pl = PlayLayer::get();
		return pl && pl->m_isPracticeMode;
	}
}

class $modify(BotBaseLayer, GJBaseGameLayer) {
	void processCommands(float dt) {
		if (Bot::mode == Mode::Play && Bot::playbackAllowed()) {
			while (Bot::cursor < Bot::inputs.size() && Bot::inputs[Bot::cursor].frame <= Bot::frame) {
				auto& in = Bot::inputs[Bot::cursor];
				Bot::injecting = true;
				GJBaseGameLayer::handleButton(in.down, in.button, in.player1);
				Bot::injecting = false;
				++Bot::cursor;
			}
		}

		GJBaseGameLayer::processCommands(dt);

		if (Bot::mode != Mode::Idle) ++Bot::frame;
	}

	void handleButton(bool down, int button, bool isPlayer1) {
		if (Bot::mode == Mode::Record && !Bot::injecting) {
			Bot::inputs.push_back({Bot::frame, button, down, isPlayer1});
		}
		GJBaseGameLayer::handleButton(down, button, isPlayer1);
	}
};

class $modify(BotPlayLayer, PlayLayer) {
	void resetLevel() {
		PlayLayer::resetLevel();
		Bot::frame = 0;
		Bot::cursor = 0;
		if (Bot::mode == Mode::Record) Bot::inputs.clear();
	}

	void levelComplete() {
		if (Bot::mode == Mode::Record) {
			Bot::save(Bot::currentLevelID());
			Notification::create("Replay saved", NotificationIcon::Success)->show();
		}
		PlayLayer::levelComplete();
	}

	void onQuit() {
		Bot::mode = Mode::Idle;
		PlayLayer::onQuit();
	}
};

class $modify(BotPauseLayer, PauseLayer) {
	void customSetup() {
		PauseLayer::customSetup();

		auto menu = CCMenu::create();
		auto size = CCDirector::get()->getWinSize();
		menu->setPosition({size.width - 70.f, 70.f});

		auto rec = CCMenuItemSpriteExtra::create(
			ButtonSprite::create("Record"), this, menu_selector(BotPauseLayer::onBotRecord));
		auto play = CCMenuItemSpriteExtra::create(
			ButtonSprite::create("Play"), this, menu_selector(BotPauseLayer::onBotPlay));
		auto stop = CCMenuItemSpriteExtra::create(
			ButtonSprite::create("Stop"), this, menu_selector(BotPauseLayer::onBotStop));

		rec->setPosition({0.f, 70.f});
		play->setPosition({0.f, 0.f});
		stop->setPosition({0.f, -70.f});

		menu->addChild(rec);
		menu->addChild(play);
		menu->addChild(stop);
		this->addChild(menu);
	}

	void onBotRecord(CCObject*) {
		Bot::mode = Mode::Record;
		Bot::inputs.clear();
		Notification::create("Recording next attempt", NotificationIcon::Info)->show();
		this->onRestart(nullptr);
	}

	void onBotPlay(CCObject*) {
		if (!Bot::playbackAllowed()) {
			Notification::create("Blocked: enable practice mode or your safe-mode mod", NotificationIcon::Error)->show();
			return;
		}
		if (!Bot::load(Bot::currentLevelID())) {
			Notification::create("No replay saved for this level", NotificationIcon::Error)->show();
			return;
		}
		Bot::mode = Mode::Play;
		Notification::create("Playing replay", NotificationIcon::Info)->show();
		this->onRestart(nullptr);
	}

	void onBotStop(CCObject*) {
		Bot::mode = Mode::Idle;
		Notification::create("Bot stopped", NotificationIcon::Info)->show();
	}
};
