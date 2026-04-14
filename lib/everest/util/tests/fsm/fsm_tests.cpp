// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/util/fsm/fsm.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

// --- Minimal test state infrastructure ---

enum class StateID {
    A,
    B,
    C,
};

enum class Event {
    GoToB,
    GoToC,
    Stay,
    Unknown,
};

struct TestState;
using TestStatePtr = std::unique_ptr<TestState>;

struct FeedResult {
    FeedResult() = default;
    FeedResult(bool handled) : unhandled(!handled) {
    }
    FeedResult(TestStatePtr result_state) : unhandled(false), new_state(std::move(result_state)) {
    }

    bool unhandled{true};
    TestStatePtr new_state{nullptr};
};

struct TestState {
    using ContainerType = TestStatePtr;
    using EventType = Event;

    TestState(StateID id, std::vector<std::string>& log) : m_id(id), m_log(log) {
    }

    virtual ~TestState() = default;

    StateID get_id() const {
        return m_id;
    }

    virtual void enter() {
        m_log.push_back("enter:" + state_name());
    }

    virtual FeedResult feed(Event ev) = 0;

    virtual void leave() {
        m_log.push_back("leave:" + state_name());
    }

    std::string state_name() const {
        switch (m_id) {
        case StateID::A:
            return "A";
        case StateID::B:
            return "B";
        case StateID::C:
            return "C";
        }
        return "?";
    }

protected:
    StateID m_id;
    std::vector<std::string>& m_log;
};

struct StateA : TestState {
    StateA(std::vector<std::string>& log) : TestState(StateID::A, log) {
    }

    FeedResult feed(Event ev) override;
};

struct StateB : TestState {
    StateB(std::vector<std::string>& log) : TestState(StateID::B, log) {
    }

    FeedResult feed(Event ev) override {
        if (ev == Event::Stay) {
            return FeedResult(true);
        }
        return {};
    }
};

FeedResult StateA::feed(Event ev) {
    if (ev == Event::GoToB) {
        return FeedResult(std::make_unique<StateB>(m_log));
    }
    if (ev == Event::Stay) {
        return FeedResult(true);
    }
    return {}; // unhandled
}

// --- FSM Tests ---

TEST(FsmV2Test, ConstructionCallsEnter) {
    std::vector<std::string> log;
    {
        fsm::v2::FSM<TestState> fsm(std::make_unique<StateA>(log));
        EXPECT_EQ(log.size(), 1u);
        EXPECT_EQ(log[0], "enter:A");
    }
}

TEST(FsmV2Test, DestructionCallsLeave) {
    std::vector<std::string> log;
    {
        fsm::v2::FSM<TestState> fsm(std::make_unique<StateA>(log));
        log.clear();
    }
    EXPECT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0], "leave:A");
}

TEST(FsmV2Test, FeedNoTransition) {
    std::vector<std::string> log;
    fsm::v2::FSM<TestState> fsm(std::make_unique<StateA>(log));

    auto result = fsm.feed(Event::Stay);
    EXPECT_TRUE(result);
    EXPECT_FALSE(result.transitioned());
    EXPECT_EQ(fsm.get_current_state_id(), StateID::A);
}

TEST(FsmV2Test, FeedWithTransition) {
    std::vector<std::string> log;
    fsm::v2::FSM<TestState> fsm(std::make_unique<StateA>(log));
    log.clear();

    auto result = fsm.feed(Event::GoToB);
    EXPECT_TRUE(result);
    EXPECT_TRUE(result.transitioned());
    EXPECT_EQ(fsm.get_current_state_id(), StateID::B);

    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0], "leave:A");
    EXPECT_EQ(log[1], "enter:B");
}

TEST(FsmV2Test, UnhandledEvent) {
    std::vector<std::string> log;
    fsm::v2::FSM<TestState> fsm(std::make_unique<StateA>(log));

    auto result = fsm.feed(Event::Unknown);
    EXPECT_FALSE(result); // FeedResult is falsy for unhandled
    EXPECT_FALSE(result.transitioned());
    EXPECT_EQ(fsm.get_current_state_id(), StateID::A);
}

TEST(FsmV2Test, GetCurrentStateId) {
    std::vector<std::string> log;
    fsm::v2::FSM<TestState> fsm(std::make_unique<StateA>(log));
    EXPECT_EQ(fsm.get_current_state_id(), StateID::A);

    fsm.feed(Event::GoToB);
    EXPECT_EQ(fsm.get_current_state_id(), StateID::B);
}

// --- FeedResult with output ---

struct OutputState;
using OutputStatePtr = std::unique_ptr<OutputState>;

struct OutputFeedResult {
    OutputFeedResult() = default;
    OutputFeedResult(bool handled) : unhandled(!handled) {
    }
    OutputFeedResult(int output_value, bool handled) : unhandled(!handled), output(output_value) {
    }
    OutputFeedResult(OutputStatePtr result_state) : unhandled(false), new_state(std::move(result_state)) {
    }
    OutputFeedResult(OutputStatePtr result_state, int output_value) :
        unhandled(false), new_state(std::move(result_state)), output(output_value) {
    }

    bool unhandled{true};
    OutputStatePtr new_state{nullptr};
    int output{0};
};

struct OutputState {
    using ContainerType = OutputStatePtr;
    using EventType = Event;

    OutputState(StateID id) : m_id(id) {
    }

    virtual ~OutputState() = default;

    StateID get_id() const {
        return m_id;
    }

    virtual void enter() {
    }

    virtual OutputFeedResult feed(Event ev) = 0;

    virtual void leave() {
    }

protected:
    StateID m_id;
};

struct OutputStateA : OutputState {
    OutputStateA() : OutputState(StateID::A) {
    }

    OutputFeedResult feed(Event ev) override {
        if (ev == Event::Stay) {
            return OutputFeedResult(42, true);
        }
        if (ev == Event::GoToB) {
            return OutputFeedResult(std::make_unique<OutputStateA>(), 99);
        }
        return {};
    }
};

TEST(FsmV2Test, FeedResultWithOutput) {
    fsm::v2::FSM<OutputState> fsm(std::make_unique<OutputStateA>());

    auto result = fsm.feed(Event::Stay);
    EXPECT_TRUE(result);
    EXPECT_FALSE(result.transitioned());
    EXPECT_EQ(result.output, 42);
}

TEST(FsmV2Test, FeedResultWithOutputOnTransition) {
    fsm::v2::FSM<OutputState> fsm(std::make_unique<OutputStateA>());

    auto result = fsm.feed(Event::GoToB);
    EXPECT_TRUE(result);
    EXPECT_TRUE(result.transitioned());
    EXPECT_EQ(result.output, 99);
}

TEST(FsmV2Test, FeedResultVoidUnhandled) {
    std::vector<std::string> log;
    fsm::v2::FSM<TestState> fsm(std::make_unique<StateA>(log));

    auto result = fsm.feed(Event::Unknown);
    // FeedResult<void> — no .output member, just check bool and transitioned
    EXPECT_FALSE(result);
    EXPECT_FALSE(result.transitioned());
}

// --- NestedFSM tests ---

struct NestedState;
using NestedStatePtr = std::unique_ptr<NestedState>;

struct NestedFeedResult {
    NestedFeedResult() = default;
    NestedFeedResult(bool handled) : unhandled(!handled) {
    }
    NestedFeedResult(NestedStatePtr result_state) : unhandled(false), new_state(std::move(result_state)) {
    }

    bool unhandled{true};
    NestedStatePtr new_state{nullptr};
};

struct NestedState {
    using ContainerType = NestedStatePtr;
    using EventType = Event;

    NestedState(StateID id, std::vector<std::string>& log) : m_id(id), m_log(log) {
    }

    virtual ~NestedState() = default;

    StateID get_id() const {
        return m_id;
    }

    virtual void enter() {
        m_log.push_back("enter:" + state_name());
    }

    virtual NestedFeedResult feed(Event ev) = 0;

    virtual void leave() {
        m_log.push_back("leave:" + state_name());
    }

    virtual NestedStatePtr get_initial() {
        return nullptr;
    }

    std::string state_name() const {
        switch (m_id) {
        case StateID::A:
            return "A";
        case StateID::B:
            return "B";
        case StateID::C:
            return "C";
        }
        return "?";
    }

protected:
    StateID m_id;
    std::vector<std::string>& m_log;
};

// ChildB is a leaf child of ParentA
struct ChildB : NestedState {
    ChildB(std::vector<std::string>& log) : NestedState(StateID::B, log) {
    }

    NestedFeedResult feed(Event ev) override {
        if (ev == Event::Stay) {
            return NestedFeedResult(true);
        }
        return {}; // bubble up to parent
    }
};

// ParentA has ChildB as initial child state
struct ParentA : NestedState {
    ParentA(std::vector<std::string>& log) : NestedState(StateID::A, log) {
    }

    NestedFeedResult feed(Event ev) override {
        if (ev == Event::GoToC) {
            // Transition to a new state C (leaf, no children)
            return NestedFeedResult(std::make_unique<LeafC>(m_log));
        }
        return {};
    }

    NestedStatePtr get_initial() override {
        return std::make_unique<ChildB>(m_log);
    }

    struct LeafC : NestedState {
        LeafC(std::vector<std::string>& log) : NestedState(StateID::C, log) {
        }

        NestedFeedResult feed(Event) override {
            return {};
        }
    };
};

TEST(NestedFsmV2Test, ConstructionUnrollsChildren) {
    std::vector<std::string> log;
    {
        fsm::v2::NestedFSM<NestedState> fsm(std::make_unique<ParentA>(log));
        // Should enter ParentA, then enter ChildB
        ASSERT_EQ(log.size(), 2u);
        EXPECT_EQ(log[0], "enter:A");
        EXPECT_EQ(log[1], "enter:B");
    }
}

TEST(NestedFsmV2Test, DestructionLeavesAllStates) {
    std::vector<std::string> log;
    {
        fsm::v2::NestedFSM<NestedState> fsm(std::make_unique<ParentA>(log));
        log.clear();
    }
    // Should leave ChildB then ParentA
    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0], "leave:B");
    EXPECT_EQ(log[1], "leave:A");
}

TEST(NestedFsmV2Test, LeafHandlesEvent) {
    std::vector<std::string> log;
    fsm::v2::NestedFSM<NestedState> fsm(std::make_unique<ParentA>(log));

    auto result = fsm.feed(Event::Stay);
    EXPECT_TRUE(result);
    EXPECT_FALSE(result.transitioned());
}

TEST(NestedFsmV2Test, EventBubblesToParent) {
    std::vector<std::string> log;
    fsm::v2::NestedFSM<NestedState> fsm(std::make_unique<ParentA>(log));
    log.clear();

    // GoToC is unhandled by ChildB, bubbles to ParentA which transitions
    auto result = fsm.feed(Event::GoToC);
    EXPECT_TRUE(result);
    EXPECT_TRUE(result.transitioned());

    // ChildB leave, ParentA leave (popped off stack), then new LeafC enter
    ASSERT_EQ(log.size(), 3u);
    EXPECT_EQ(log[0], "leave:B");
    EXPECT_EQ(log[1], "leave:A");
    EXPECT_EQ(log[2], "enter:C");
}

TEST(NestedFsmV2Test, UnhandledByAll) {
    std::vector<std::string> log;
    fsm::v2::NestedFSM<NestedState> fsm(std::make_unique<ParentA>(log));

    auto result = fsm.feed(Event::Unknown);
    EXPECT_FALSE(result);
}

TEST(NestedFsmV2Test, GetCurrentStateIdReturnsFullPath) {
    std::vector<std::string> log;
    fsm::v2::NestedFSM<NestedState> fsm(std::make_unique<ParentA>(log));

    auto ids = fsm.get_current_state_id();
    ASSERT_EQ(ids.size(), 2u);
    EXPECT_EQ(ids[0], StateID::A);
    EXPECT_EQ(ids[1], StateID::B);

    fsm.feed(Event::GoToC);
    ids = fsm.get_current_state_id();
    ASSERT_EQ(ids.size(), 1u);
    EXPECT_EQ(ids[0], StateID::C);
}
