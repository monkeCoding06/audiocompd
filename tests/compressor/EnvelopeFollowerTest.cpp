#include "TestHarness.hpp"

#include "compressor/EnvelopeFollower.hpp"

void registerEnvelopeFollowerTests(TestRunner& runner) {
    runner.add("EnvelopeFollower.ZeroAttackTracksImmediately", [] {
        audiocompd::EnvelopeFollower follower;
        follower.configure(48'000.0F, 0.0F, 0.0F);
        requireNear(follower.process(0.75F), 0.75F, 0.0001F,
                    "zero-time follower did not track the input");
    });

    runner.add("EnvelopeFollower.AttackSmoothsRisingSignal", [] {
        audiocompd::EnvelopeFollower follower;
        follower.configure(48'000.0F, 10.0F, 100.0F);
        const float first = follower.process(1.0F);
        require(first > 0.0F && first < 1.0F,
                "attack must smooth a rising signal");
    });
}

