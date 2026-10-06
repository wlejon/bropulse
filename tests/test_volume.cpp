#include <bropulse/types.h>
#include "check.h"

int main() {
    using namespace bropulse::volume;

    // 1. Cubic curve tests
    CHECK_NEAR(linear_to_cubic(0.0f), 0.0f, 0.0001f);
    CHECK_NEAR(linear_to_cubic(1.0f), 1.0f, 0.0001f);
    CHECK_NEAR(cubic_to_linear(0.0f), 0.0f, 0.0001f);
    CHECK_NEAR(cubic_to_linear(1.0f), 1.0f, 0.0001f);

    // Negative inputs handle safely
    CHECK_EQ(linear_to_cubic(-0.5f), 0.0f);
    CHECK_EQ(cubic_to_linear(-0.5f), 0.0f);

    // Roundtrip tests
    float test_vals[] = {0.05f, 0.2f, 0.5f, 0.75f, 1.0f, 1.25f};
    for (float val : test_vals) {
        float cubic = linear_to_cubic(val);
        float roundtrip = cubic_to_linear(cubic);
        CHECK_NEAR(roundtrip, val, 0.0001f);
    }

    // 2. Decibel conversion tests
    CHECK_NEAR(volume_to_db(1.0f), 0.0f, 0.001f);
    CHECK_NEAR(volume_to_db(0.5f), -6.0206f, 0.01f);
    CHECK_NEAR(volume_to_db(0.1f), -20.0f, 0.001f);
    CHECK_NEAR(volume_to_db(0.0f), -96.0f, 0.001f);

    CHECK_NEAR(db_to_volume(0.0f), 1.0f, 0.0001f);
    CHECK_NEAR(db_to_volume(-20.0f), 0.1f, 0.001f);
    CHECK_NEAR(db_to_volume(-100.0f), 0.0f, 0.0001f);

    float db_vals[] = {-40.0f, -24.0f, -12.0f, -6.0f, 0.0f, 6.0f};
    for (float db : db_vals) {
        float vol = db_to_volume(db);
        float recovered_db = volume_to_db(vol);
        CHECK_NEAR(recovered_db, db, 0.01f);
    }

    // 3. Clamping tests
    CHECK_EQ(clamp_volume(-0.5f), 0.0f);
    CHECK_EQ(clamp_volume(0.5f), 0.5f);
    CHECK_EQ(clamp_volume(1.0f), 1.0f);
    CHECK_EQ(clamp_volume(1.8f), 1.5f);
    CHECK_EQ(clamp_volume(0.8f, 0.2f, 0.6f), 0.6f);
    CHECK_EQ(clamp_volume(0.1f, 0.2f, 0.6f), 0.2f);

    // 4. AudioStream equality checks
    bropulse::AudioStream s1;
    s1.id = 1;
    s1.name = "Music";
    s1.volume = 0.8f;
    s1.is_muted = false;
    s1.sink_id = 10;

    bropulse::AudioStream s2 = s1;
    CHECK_EQ(s1, s2);

    s2.volume = 0.5f;
    CHECK_NE(s1, s2);

    // 5. AudioSink equality checks
    bropulse::AudioSink k1;
    k1.id = 10;
    k1.name = "Speakers";
    k1.description = "Built-in Speakers";
    k1.volume = 1.0f;
    k1.is_default = true;

    bropulse::AudioSink k2 = k1;
    CHECK_EQ(k1, k2);

    k2.is_default = false;
    CHECK_NE(k1, k2);

    return bstest::finish("test_volume");
}
