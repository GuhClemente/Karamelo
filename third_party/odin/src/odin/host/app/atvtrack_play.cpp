/*
 *  ██████╗ ██████╗ ██╗███╗   ██╗
 * ██╔═══██╗██╔══██╗██║████╗  ██║
 * ██║   ██║██║  ██║██║██╔██╗ ██║
 * ██║   ██║██║  ██║██║██║╚██╗██║
 * ╚██████╔╝██████╔╝██║██║ ╚████║
 *  ╚═════╝ ╚═════╝ ╚═╝╚═╝  ╚═══╝
 *
 * Copyright (C) 2026 Réda Chérif-Touil
 * Copyright (C) 2026 Daniel Martin
 *
 * Odin is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * Odin is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with Odin.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "odin/host/app/atvtrack_play.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "odin/host/app/options.hpp"
#include "odin/host/app/user_files.hpp"
#include "odin/host/audio/audio_out.hpp"
#include "odin/host/input/controls.hpp"
#include "odin/host/input/outputs_network.hpp"
#include "odin/host/ui/menu_session.hpp"
#include "odin/machine/boards/atvtrack/atvtrack.hpp"
#include "odin/machine/cabinet/drive_board.hpp"

namespace odin {

namespace {

/* The operator panel, on the same pins of every board (the test page of
 * each game): SERVICE, TEST, the one coin entry, VOLUME + (the volume is a
 * control of the game list too). Pin 0.6 carries the data of the ADC. */
constexpr PortSwitch SERVICE_PIN{ 0, 1 }, TEST_PIN{ 0, 3 }, COIN_PIN{ 0, 5 }, VOLUME_UP_PIN{ 0, 0 }, VOLUME_DOWN_PIN{ 0, 2 };
/* SERVICE of player 1 in CabinetInputs (control_panel.cpp) */
constexpr uint16_t SERVICE = 0x4000;
constexpr unsigned COIN_FRAMES = 6;	/* HEURISTIC: a coin's pulse */
constexpr int DAC_RATE = AtvTrack::DAC_RATE;
/* Smashing Drive keeps its volume, 0 on a new board. At each power-on Odin
 * sets the level of [sound.<game>] start_volume (options.hpp), as an
 * operator would: VOLUME - held from frame 60 for as long as the game takes
 * from 96 down to 0, then VOLUME + the frames that take it from 0 to each of
 * START_VOLUMES (measured). HEURISTIC: VOLUME_DOWN_FRAMES and VOLUME_GAP,
 * chosen with room to spare (96 is down to 0 within them, checked). */
constexpr unsigned VOLUME_UP_FRAMES[] = { 0, 96, 122, 147, 180 };
static_assert(std::size(VOLUME_UP_FRAMES) == std::size(START_VOLUMES));
constexpr unsigned VOLUME_FROM = 60, VOLUME_DOWN_FRAMES = 200, VOLUME_GAP = 10;
/* Kick on the right stick: past KICK_FROM, power is the peak within
 * KICK_RISE frames, then back under KICK_REST to rearm. Pulling back first
 * lifts the ball, until the stick rests BACKSWING_FRAMES. HEURISTIC: all of
 * these, and the ball's height below (no dump of the sensors' PIC). */
constexpr unsigned KICK_FRAMES = 3, KICK_RISE = 3, BACKSWING_FRAMES = 20;
constexpr float KICK_FROM = 0.3f, KICK_REST = 0.2f;
/* The program's kick power, up to 127: the root of both side sensors
 * squared, times the gain (0x8C02F5E0); Odin gives it 10 to 127.
 * HEURISTIC: a real foot's values are unknown. Up to 50
 * passes and past 80 clears the field, so travel is cubed. */
constexpr float KICK_SOFT = 10.0f, KICK_HARD = 127.0f, KICK_GAIN = 1.2f;

class AtvGame final : public Board {
public:
	AtvGame(const Game &game, Controls &controls, Display &display, const std::string &kept,
	        std::optional<unsigned> volume_frames, bool verbose);
	~AtvGame() override;

	const std::string &game_name() const override { return m_name; }
	unsigned frame() const override { return m_board->frame(); }
	uint64_t frame_nanoseconds() const override { return m_board->frame_nanoseconds(); }
	void run_frame() override;
	bool restart_asked() const override { return false; }
	bool save_state(const std::string &path) override { return m_board->save_state(path); }
	bool set_link(const NetLinkSettings &link) { return m_board->set_link(link); }
	bool set_flash_setting(uint32_t byte, unsigned shift, unsigned bits, uint8_t value)
	{
		return m_board->set_flash_setting(byte, shift, bits, value);
	}
	bool set_nand_setting(uint32_t byte, unsigned shift, unsigned bits, uint8_t value,
	                      std::optional<uint8_t> only_from = std::nullopt)
	{
		return m_board->set_nand_setting(byte, shift, bits, value, only_from);
	}
	bool load_state(const std::string &path) override;
	std::span<const uint8_t> io_outputs() const override { return m_outputs; }
	std::span<const uint8_t> output_levels() const override { return m_levels; }
	LinkStatus link_status() const override
	{
		const GaelcoLink *const link = m_board->link();
		LinkStatus status;

		status.failed = m_board->link_failed();
		status.board = link || status.failed;
		status.linked = link != nullptr;
		status.previous = link && link->previous_linked();
		status.next = link && link->next_linked();
		return status;
	}
	const std::vector<int16_t> &sound(bool optional) const override
	{
		return optional ? m_board->woofer() : m_board->sound();
	}
	/* The wheel motor's two channels, one for each way (the OUTPUT TEST of
	 * Smashing Drive: WHEEL MOTOR +1.00 is channel 0 at 255, -1.00 channel
	 * 1), as a drive board's torque, the right positive */
	std::optional<int> drive_torque() const override
	{
		if (!m_wheel_motor)
			return std::nullopt;

		const auto &motors = m_board->motors();

		return (static_cast<int>(motors[0]) - static_cast<int>(motors[1])) * DriveBoard::MOST / 255;
	}

private:
	struct Kick {
		float x = 0.0f, forward = 0.0f, back = 0.0f;
		unsigned rise = 0, frames = 0, rested = 0;
		bool spent = false;
		AtvTrack::BallSensors::value_type sensors = AtvTrack::BALL_AT_REST;
	};

	void read_controls();
	void read_kick(const CabinetInputs &in, unsigned player);

	std::string m_name, m_kept;
	std::unique_ptr<AtvTrack> m_board;
	Controls &m_controls;
	Display &m_display;
	/* The controls of the game and the pins its wiring grounds (game list) */
	std::vector<GameControl> m_wiring;
	std::vector<PortSwitch> m_grounded;
	std::vector<unsigned> m_grounded_analog;
	/* The channels of CabinetInputs of the stick that kicks each ball, side
	 * then forward; none for a game without balls */
	std::array<std::optional<std::array<uint8_t, 2>>, 2> m_balls;
	/* The frames VOLUME - then VOLUME + are held at power-on (0: not) */
	unsigned m_volume_down, m_volume_up;
	unsigned m_coin = 0;
	Kick m_kick[2];
	/* The outputs as JVS counts them: output n is bit n - 1 of the
	 * system pins, then a channel of the motor DAC that runs (GameOutput) */
	std::array<uint8_t, 3> m_outputs{};
	/* How hard each one drives: a pin on, all; a channel, its level */
	std::array<uint8_t, 24> m_levels{};
	bool m_wheel_motor;
};

std::unique_ptr<AtvTrack>
make_board(const Game &game)
{
	if (game.machine == "smashdrv") {
		const std::span<const uint8_t> program = game.section("program");

		return std::make_unique<Ref010131>(AtvTrack::rom_of(game.section("sdra"), game.section("sdrb"), game.section("sdrc")),
		                                  std::vector<uint8_t>(program.begin(), program.end()));
	}
	std::array<std::vector<uint8_t>, NandBank::CHIPS> nand;

	for (unsigned c = 0; c < NandBank::CHIPS; c++) {
		const std::span<const uint8_t> chip = game.section("nand" + std::to_string(c));

		nand[c].assign(chip.begin(), chip.end());
	}
	return std::make_unique<Ref020419>(std::move(nand), game.section("fpga"));
}

AtvGame::AtvGame(const Game &game, Controls &controls, Display &display, const std::string &kept,
                 std::optional<unsigned> volume_frames, bool verbose)
	: m_name(game.name), m_kept(kept), m_board(make_board(game)), m_controls(controls), m_display(display),
	  m_wiring(game.controls), m_grounded(game.grounded_pins), m_grounded_analog(game.grounded_analog),
	  m_volume_down(volume_frames ? VOLUME_DOWN_FRAMES : 0), m_volume_up(volume_frames.value_or(0)),
	  m_wheel_motor(game.wheel_motor)
{
	for (const GameControl &control : m_wiring)
		if (control.ball != GameControl::Ball::NONE) {
			std::optional<std::array<uint8_t, 2>> &stick = m_balls[control.player];

			if (!stick)
				stick.emplace();
			(*stick)[control.ball == GameControl::Ball::FORWARD] = control.analog;
		}
	m_board->set_name(m_name);
	m_board->set_verbose(verbose);
	m_board->set_motion_platform(game.motion_platform);
	if (!m_kept.empty())
		m_board->load_kept(m_kept);
	m_board->reset();
}

AtvGame::~AtvGame()
{
	if (!m_kept.empty())
		m_board->save_kept(m_kept);
}

bool
AtvGame::load_state(const std::string &path)
{
	return m_board->load_state(path);
}

/* The value of a channel of the ADC for a channel of CabinetInputs, from
 * span[0] at the low end of the control to span[1] at its high end */
uint8_t
adc_value(const std::array<uint8_t, 2> &span, uint16_t channel)
{
	return static_cast<uint8_t>(std::lround(span[0] + (span[1] - span[0]) * (channel / 65535.0)));
}

/* Right stick: forward sets power, sideways direction, pull-back height. The
 * game reads power from the side sensors, height from the middle one,
 * direction from right minus left (BallTest page). */
void
AtvGame::read_kick(const CabinetInputs &in, unsigned player)
{
	Kick &kick = m_kick[player];
	const auto axis = [&](unsigned half) {
		return (static_cast<float>(in.jvs_analog[(*m_balls[player])[half]]) - 32768.0f) / 32768.0f;
	};
	const float x = axis(0), forward = -axis(1), far = std::hypot(x, forward);

	kick.back = std::max(kick.back, -forward);
	kick.rested = far < KICK_REST ? kick.rested + 1 : 0;
	if (kick.rested >= BACKSWING_FRAMES)
		kick.back = 0.0f;

	if (kick.frames > 0) {
		if (--kick.frames == 0)
			kick.sensors = AtvTrack::BALL_AT_REST;
	} else if (kick.spent) {
		kick.spent = forward > KICK_REST;
	} else if (forward > KICK_FROM) {
		if (forward >= kick.forward) {
			kick.forward = forward;
			kick.x = x;
		}
		if (++kick.rise >= KICK_RISE) {
			const float travel = std::clamp((std::hypot(kick.x, kick.forward) - KICK_FROM) / (1.0f - KICK_FROM), 0.0f, 1.0f);
			const float power = KICK_SOFT + (KICK_HARD - KICK_SOFT) * travel * travel * travel;
			const float force = power / KICK_GAIN / std::sqrt(2.0f * (1.0f + kick.x * kick.x));
			const auto side = [&](float turned) { return static_cast<uint8_t>(128.0f + force * (1.0f + turned)); };

			kick.sensors = { static_cast<uint8_t>(0x30 + static_cast<int>(kick.back * 200.0f)), side(-kick.x), side(kick.x) };
			kick.frames = KICK_FRAMES;
			kick.spent = true;
			kick.back = kick.forward = 0.0f;
			kick.rise = 0;
		}
	} else {
		kick.forward = 0.0f;
		kick.rise = 0;
	}
}

void
AtvGame::read_controls()
{
	CabinetInputs in{};
	std::array<uint8_t, 4> switches{};
	std::array<uint8_t, 4> analog{ 0x80, 0x80, 0x80, 0x80 };
	const auto close = [&](PortSwitch pin) {
		switches[pin.group] = static_cast<uint8_t>(switches[pin.group] | 1u << pin.bit);
	};

	m_controls.read_inputs(in);
	if (in.jvs_coin[0])
		m_coin = COIN_FRAMES;
	if (m_coin > 0) {
		close(COIN_PIN);
		m_coin--;
	}
	if (in.jvs_system & 0x80)
		close(TEST_PIN);
	if (in.jvs_player[0] & SERVICE)
		close(SERVICE_PIN);
	/* The volume set at power-on: VOLUME - down to 0, then VOLUME + */
	const unsigned up_from = VOLUME_FROM + m_volume_down + VOLUME_GAP;

	if (frame() >= VOLUME_FROM && frame() < VOLUME_FROM + m_volume_down)
		close(VOLUME_DOWN_PIN);
	else if (m_volume_down && frame() >= up_from && frame() < up_from + m_volume_up)
		close(VOLUME_UP_PIN);
	for (const GameControl &control : m_wiring) {
		if (control.port && in.jvs_player[control.button.player & 1] & control.button.mask)
			close(*control.port);
		if (control.span)
			analog[control.analog] = adc_value(*control.span, in.jvs_analog[control.analog]);
	}
	for (const PortSwitch pin : m_grounded)
		close(pin);
	for (const unsigned channel : m_grounded_analog)
		analog[channel] = 0;
	if (m_balls[0] || m_balls[1]) {
		for (unsigned player = 0; player < 2; player++)
			if (m_balls[player])
				read_kick(in, player);
		m_board->set_ball_sensors({ m_kick[0].sensors, m_kick[1].sensors },
		                          { m_kick[0].frames > 0, m_kick[1].frames > 0 });
	}
	m_board->set_switches(switches);
	m_board->set_analog(analog);
}

void
AtvGame::run_frame()
{
	read_controls();
	const DisplayUpscaleMode mode = m_display.upscale_mode();

	/* Fall back to Snap without geometry shaders, as Renderer::draw_mesh
	 * does */
	m_board->set_look(m_display.scale(), static_cast<unsigned>(mode == DisplayUpscaleMode::NATIVE && !m_display.draws_native()
	                                                                   ? DisplayUpscaleMode::SNAP
	                                                                   : mode),
	                  m_display.sorts_per_pixel());
	m_board->run_frame();
	m_outputs.fill(0);
	m_levels.fill(0);
	for (unsigned bit = 0; bit < 11; bit++)
		if (m_board->system_pins() & 1u << bit) {
			m_outputs[bit / 8] = static_cast<uint8_t>(m_outputs[bit / 8] | 0x80u >> (bit % 8));
			m_levels[bit] = 255;
		}
	for (unsigned channel = 0; channel < 4; channel++) {
		m_levels[16 + channel] = m_board->motors()[channel];
		if (m_levels[16 + channel])
			m_outputs[2] = static_cast<uint8_t>(m_outputs[2] | 0x80u >> channel);
	}
	if (PmxScene scene = m_board->take_scene(); scene.rendered)
		m_display.set_scene(std::move(scene));
	m_display.set_blank(m_board->dac_blanked());
}

} /* namespace */

Ended
play_atvtrack(const Options &options, const Game &game, const GameList &games, Settings &settings,
              const std::string &settings_file, Display &display, Keyboard &keys, Pointers *pointers, Opened &opened)
{
	Controls controls(keys, game.controls, game.outputs, settings, game.name);

	controls.set_pointers(pointers);
	controls.set_wheel_rotation(wheel_rotation_of(settings, game.name));
	/* The pads shake and the wheel turns unless [players.<game>] says off */
	controls.feel_game(game, vibration_on(settings, game.name));
	controls.set_wheel_force(wheel_force_of(settings, game.name));
	controls.set_vibration_force(vibration_force_of(settings, game.name));

	const bool network = network_on(settings);
	std::unique_ptr<OutputsNetwork> outputs =
		network ? OutputsNetwork::open(game.name, game.outputs) : nullptr;

	/* The DAC's rate, its woofer as game_sound says. None at unthrottled
	 * frames, which do not keep time (-u). */
	Options sound_options = options;
	const GameSound sound = game_sound(sound_options, game, settings);
	std::unique_ptr<AudioOut> audio;

	if (sound_options.sound && options.unthrottled && options.sound_asked)
		std::fprintf(stderr, "sound: not with -u\n");
	else if (sound_options.sound && !options.unthrottled)
		audio = AudioOut::make(*sound_options.sound, sound.level, options.sound_device, DAC_RATE);
	if (audio && game.woofer)
		audio->set_woofer(sound.woofer_hz);
	if (audio && options.volume != 100)
		audio->set_volume(static_cast<float>(options.volume) / 100.0f);
	if (audio && sound_options.sound == AudioOut::Mode::MIX && sound.level != 0)
		std::printf("sound: the woofer at %+g dB in the mix\n", sound.level);

	std::optional<unsigned> volume_frames;

	if (game.machine == "smashdrv") {
		std::string level = START_VOLUMES[START_VOLUME_STANDARD];

		for (const auto &[key, value] : settings.section("sound." + game.name))
			if (key == "start_volume")
				level = value;

		const auto at = std::find(std::begin(START_VOLUMES), std::end(START_VOLUMES), level);

		if (at == std::end(START_VOLUMES))
			refuse_setting("sound." + game.name, "start_volume", level);
		volume_frames = VOLUME_UP_FRAMES[at == std::end(START_VOLUMES) ? START_VOLUME_STANDARD
		                                                               : at - std::begin(START_VOLUMES)];
	}

	AtvGame running(game, controls, display, SaveFolder::of_game(game.name, options.saves).path("kept.bin"),
	                volume_frames, options.verbose);

	display.attach_scenes(game.name, game.title);

	bool started = true;

	/* The link cable of the games that have one (Game::Link) */
	const NetLinkSettings link = link_settings(settings);

	if (game.link.cabinets && !running.set_link(link))
		std::fprintf(stderr, "ERROR: no link cable\n");
	/* Its Link Mode, from this Odin's cabinet: 1 the master, as its test
	 * menu sets it (MASTER 1, SLAVE 2); without the cable, NO LINK (0: both
	 * games then play alone), as Hikaru's network board sets it */
	const bool flash = game.link.seat_in == Game::Link::In::FLASH, nand = game.link.seat_in == Game::Link::In::NAND;
	const bool off = link.board != NetLinkSettings::Board::RING;

	if ((flash || nand) && !off && link.cabinet > game.link.cabinets)
		std::fprintf(stderr, "ERROR: link: %s links %u cabinets at most: cabinet %u keeps its Link Mode\n",
		             game.title.c_str(), game.link.cabinets, link.cabinet);
	else if ((flash || nand) && (off || link.cabinet)) {
		const auto seat = static_cast<uint8_t>(off ? 0 : game.link.seat_first + link.cabinet - 1);
		const bool set = flash ? running.set_flash_setting(game.link.seat_byte, game.link.seat_shift,
		                                                   game.link.seat_bits, seat)
		                       : running.set_nand_setting(game.link.seat_byte, game.link.seat_shift,
		                                                  game.link.seat_bits, seat);

		if (!set && !off)
			std::fprintf(stderr, "link: %s keeps no settings yet: set its Link Mode once in its test menu\n",
			             game.name.c_str());
	}
	/* What a cabinet asks of the game's settings, as its operator sets them
	 * in the test menu: the cabinet played, its value; another one, the
	 * standard cabinet's value back, only from its own */
	for (const Game::Cabinet &model : game.cabinets) {
		if (!model.nand_setting)
			continue;

		const Game::Cabinet::Setting &setting = *model.nand_setting;
		const bool played = game.cabinet == model.id;

		if (!running.set_nand_setting(setting.byte, 0, 8, played ? setting.value : setting.standard,
		                              played ? std::nullopt : std::optional<uint8_t>(setting.value)) &&
		    played)
			std::fprintf(stderr, "cabinet: %s keeps no settings yet: set its %s cabinet once in its test menu\n",
			             game.name.c_str(), model.name.c_str());
	}

	if (!options.state_path.empty()) {
		started = running.load_state(options.state_path);
		if (started)
			std::printf("loaded state '%s', frame %u\n", options.state_path.c_str(), running.frame());
	}

	Ended ended = Ended::FAILED;

	if (started) {
		Session session(sound_options, game, running, nullptr, display, keys, controls, audio.get(),
		                std::move(outputs), network, settings, settings_file, games);

		std::printf("Running\n");
		ended = session.run();
		opened = session.opened();
	}
	std::printf("Finalizing\n");
	display.detach();
	return ended;
}

} /* namespace odin */
