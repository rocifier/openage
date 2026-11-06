// Copyright 2023-2024 the openage authors. See copying.md for legal info.

#include "engine.h"

#include <condition_variable>
#include <exception>
#include <future>
#include <mutex>

#include "audio/settings.h"
#include "cvar/cvar.h"
#include "gamestate/simulation.h"
#include "log/log.h"
#include "log/message.h"
#include "presenter/presenter.h"
#include "time/clock.h"
#include "time/time_loop.h"


namespace openage::engine {

Engine::Engine(mode mode,
               const util::Path &root_dir,
               const std::vector<std::string> &mods,
               const renderer::window_settings &window_settings,
               bool fvd_lobby_enabled,
               std::chrono::milliseconds network_update_interval,
               bool audio_enabled,
               bool native_config) :
	running{false},
	run_mode{mode},
	root_dir{root_dir},
	window_settings{window_settings},
	threads{} {
	log::log(INFO
	         << "launching engine with root directory"
	         << root_dir);

	// read and apply the configuration files
	this->cvar_manager = std::make_shared<cvar::CVarManager>(this->root_dir["cfg"], native_config);
	// Register before loading so the AUDIO_* values in audio.oac apply.
	auto audio_settings = std::make_shared<audio::Settings>();
	audio::register_cvars(*this->cvar_manager, audio_settings);
	cvar_manager->load_all();

	// time loop
	this->time_loop = std::make_shared<time::TimeLoop>();
	if (fvd_lobby_enabled and mode == mode::FULL) {
		this->time_loop->get_clock()->pause();
	}

	// game simulation
	this->simulation = std::make_shared<gamestate::GameSimulation>(this->root_dir,
	                                                               this->cvar_manager,
	                                                               this->time_loop);
	this->simulation->set_modpacks(mods);

	// presenter (optional)
	if (this->run_mode == mode::FULL) {
		this->presenter = std::make_shared<presenter::Presenter>(this->root_dir,
		                                                         this->simulation,
		                                                         this->time_loop);
		this->presenter->set_fvd_lobby_enabled(fvd_lobby_enabled, network_update_interval);
		this->presenter->set_audio(audio_enabled, audio_settings);
	}
}

void Engine::loop() {
	this->running = true;

	std::mutex startup_mutex;
	std::condition_variable startup_condition;
	bool presenter_initialized = false;
	bool startup_aborted = false;

	std::mutex failure_mutex;
	std::exception_ptr worker_failure{};
	auto get_worker_failure = [&]() {
		std::lock_guard lock{failure_mutex};
		return worker_failure;
	};
	auto record_worker_failure = [&](std::exception_ptr failure) {
		{
			std::lock_guard lock{failure_mutex};
			if (not worker_failure) {
				worker_failure = failure;
			}
		}
		{
			std::lock_guard lock{startup_mutex};
			startup_aborted = true;
		}
		startup_condition.notify_all();

		if (this->presenter) {
			this->presenter->stop();
		}
		this->simulation->stop();
		this->time_loop->stop();
	};
	auto stop_subsystems = [&]() {
		{
			std::lock_guard lock{startup_mutex};
			startup_aborted = true;
		}
		startup_condition.notify_all();

		if (this->presenter) {
			this->presenter->stop();
		}
		this->simulation->stop();
		this->time_loop->stop();
	};

	std::exception_ptr main_failure{};
	try {
		this->time_loop->start();
		this->threads.emplace_back([&]() {
			try {
				this->time_loop->run();
			}
			catch (...) {
				record_worker_failure(std::current_exception());
			}
		});

		if (this->run_mode == mode::FULL) {
			std::promise<void> simulation_initialized;
			auto initialized = simulation_initialized.get_future();
			this->threads.emplace_back([&, initialized_signal = std::move(simulation_initialized)]() mutable {
				bool initialization_reported = false;
				try {
					this->simulation->initialize();
					initialized_signal.set_value();
					initialization_reported = true;

					std::unique_lock lock{startup_mutex};
					startup_condition.wait(lock, [&]() {
						return presenter_initialized or startup_aborted;
					});
					if (startup_aborted) {
						return;
					}
					lock.unlock();

					this->simulation->run();
				}
				catch (...) {
					auto failure = std::current_exception();
					if (not initialization_reported) {
						initialized_signal.set_exception(failure);
					}
					record_worker_failure(failure);
				}
			});

			initialized.get();
			if (auto failure = get_worker_failure()) {
				std::rethrow_exception(failure);
			}

			log::log(INFO << "Using " << this->threads.size() + 1 << " threads "
			              << "(" << std::jthread::hardware_concurrency() << " available)");
			this->presenter->run(this->window_settings, [&]() {
				{
					std::lock_guard lock{startup_mutex};
					presenter_initialized = true;
				}
				startup_condition.notify_all();
			});
		}
		else {
			this->simulation->initialize();
			if (auto failure = get_worker_failure()) {
				std::rethrow_exception(failure);
			}

			log::log(INFO << "Using " << this->threads.size() + 1 << " threads "
			              << "(" << std::jthread::hardware_concurrency() << " available)");
			this->simulation->run();
		}
	}
	catch (...) {
		main_failure = std::current_exception();
	}

	stop_subsystems();
	this->threads.clear();

	// Qt, the window, and OpenGL resources must be destroyed on the main thread.
	this->presenter.reset();
	this->simulation.reset();
	this->time_loop.reset();
	this->running = false;

	if (main_failure) {
		std::rethrow_exception(main_failure);
	}
	if (auto failure = get_worker_failure()) {
		std::rethrow_exception(failure);
	}
}

} // namespace openage::engine
