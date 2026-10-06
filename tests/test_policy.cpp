#include <bropulse/policy.h>
#include "check.h"

int main() {
    bropulse::PolicyManager policy;

    // Available sinks for routing evaluation
    std::vector<bropulse::AudioSink> sinks;
    bropulse::AudioSink sink_speakers;
    sink_speakers.id = 1;
    sink_speakers.name = "alsa_output.speakers";
    sink_speakers.description = "Built-in Speakers";
    sinks.push_back(sink_speakers);

    bropulse::AudioSink sink_headphones;
    sink_headphones.id = 2;
    sink_headphones.name = "bluez_output.headphones";
    sink_headphones.description = "Bluetooth Headphones";
    sinks.push_back(sink_headphones);

    bropulse::AudioSink sink_hdmi;
    sink_hdmi.id = 3;
    sink_hdmi.name = "alsa_output.hdmi";
    sink_hdmi.description = "HDMI Audio";
    sinks.push_back(sink_hdmi);

    // 1. Route Rules matching
    bropulse::RouteRule rule_games;
    rule_games.id = "rule_games";
    rule_games.role_pattern = "game";
    rule_games.target_sink_name = "headphones";
    rule_games.priority = 10;
    policy.add_route_rule(rule_games);

    bropulse::RouteRule rule_media;
    rule_media.id = "rule_media";
    rule_media.role_pattern = "music|video";
    rule_media.target_sink_name = "speakers";
    rule_media.priority = 5;
    policy.add_route_rule(rule_media);

    bropulse::AudioStream game_stream;
    game_stream.id = 100;
    game_stream.name = "SuperGame";
    game_stream.app_id = "steam_app_12345";
    game_stream.media_role = "game";

    auto target_game = policy.evaluate_route(game_stream, sinks);
    REQUIRE(target_game.has_value());
    CHECK_EQ(*target_game, 2u); // Routed to headphones

    bropulse::AudioStream music_stream;
    music_stream.id = 101;
    music_stream.name = "Spotify";
    music_stream.app_id = "com.spotify.Client";
    music_stream.media_role = "music";

    auto target_music = policy.evaluate_route(music_stream, sinks);
    REQUIRE(target_music.has_value());
    CHECK_EQ(*target_music, 1u); // Routed to speakers

    // 2. Remembered route overrides rule
    policy.remember_route("com.spotify.Client", "alsa_output.hdmi");
    CHECK_TRUE(policy.remembered_route("com.spotify.Client").has_value());
    CHECK_EQ(*policy.remembered_route("com.spotify.Client"), std::string("alsa_output.hdmi"));

    target_music = policy.evaluate_route(music_stream, sinks);
    REQUIRE(target_music.has_value());
    CHECK_EQ(*target_music, 3u); // Now routed to HDMI per remembered preference!

    policy.forget_route("com.spotify.Client");
    target_music = policy.evaluate_route(music_stream, sinks);
    REQUIRE(target_music.has_value());
    CHECK_EQ(*target_music, 1u); // Falls back to rule (speakers)

    // 3. Volume rules
    bropulse::VolumeRule v_rule;
    v_rule.id = "v_clamp";
    v_rule.app_id_pattern = "steam_.*";
    v_rule.max_volume = 0.8f;
    v_rule.initial_volume = 0.5f;
    policy.add_volume_rule(v_rule);

    CHECK_TRUE(policy.initial_volume(game_stream).has_value());
    CHECK_NEAR(*policy.initial_volume(game_stream), 0.5f, 0.001f);

    CHECK_NEAR(policy.evaluate_volume(game_stream, 1.0f), 0.8f, 0.001f); // Clamped to 0.8
    CHECK_NEAR(policy.evaluate_volume(game_stream, 0.6f), 0.6f, 0.001f); // Under limit preserved

    // 4. Ducking rules
    bropulse::DuckingRule duck_rule;
    duck_rule.id = "comm_duck";
    duck_rule.trigger_role = "communication";
    duck_rule.duck_factor = 0.2f;
    policy.add_ducking_rule(duck_rule);

    bropulse::AudioStream comm_stream;
    comm_stream.id = 200;
    comm_stream.name = "Discord Voice";
    comm_stream.app_id = "com.discordapp.Discord";
    comm_stream.media_role = "communication";
    comm_stream.volume = 1.0f;

    music_stream.volume = 0.8f;

    // Both active: comm_stream + music_stream
    std::vector<bropulse::AudioStream> active = {comm_stream, music_stream};
    auto actions = policy.evaluate_ducking(active);
    REQUIRE(actions.size() == 1u);
    CHECK_EQ(actions[0].stream_id, 101u);
    CHECK_NEAR(actions[0].original_volume, 0.8f, 0.001f);
    CHECK_NEAR(actions[0].target_volume, 0.16f, 0.001f); // 0.8 * 0.2 = 0.16

    // Apply ducked volume to music_stream
    music_stream.volume = actions[0].target_volume;

    // Now comm_stream stops (only music_stream active)
    active = {music_stream};
    auto restore_actions = policy.evaluate_ducking(active);
    REQUIRE(restore_actions.size() == 1u);
    CHECK_EQ(restore_actions[0].stream_id, 101u);
    CHECK_NEAR(restore_actions[0].target_volume, 0.8f, 0.001f); // Restored to 0.8!

    return bstest::finish("test_policy");
}
