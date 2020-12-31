#include <string>
#include <array>
#include <tuple>
#include <cstdint>
#include <cstdio>

std::string expandTransitionToString(uint8_t transition)
{
    static char reason[512];
    sprintf(reason, "0x%02X, %d,%d,%d,%d to %d,%d,%d,%d",
        transition,
        (transition >> 7) & 1, (transition >> 6) & 1, (transition >> 5) & 1, (transition >> 4) & 1,
        (transition >> 3) & 1, (transition >> 2) & 1, (transition >> 1) & 1, (transition >> 0) & 1);
    return reason;
}

uint8_t motorOnToState(int which)
{
    return 1 << (3 - which);
}

uint8_t makeTransition(uint8_t oldState, uint8_t newState)
{
    return (oldState << 4) | newState;
}

int main()
{
    std::array<std::tuple<std::string, std::string>, 256> transitions;

    // initialize to invalid states
    for(int transition = 0; transition < 256; transition++) {
        transitions[transition] = {"MOTOR_STATE_NEVER", expandTransitionToString(transition)};
    }

    for(int turnOn = 0; turnOn < 4; turnOn++) {
        //step right from aligned to between
        {
            int wasOn = (turnOn - 1 + 4) % 4;
            uint8_t oldState = motorOnToState(wasOn);
            uint8_t newState = oldState | motorOnToState(turnOn);
            uint8_t transition = makeTransition(oldState, newState);
            transitions[transition] = {"MOTOR_RIGHT_ONE", expandTransitionToString(transition)};
        }
        //step left from aligned to between
        {
            int wasOn = (turnOn + 1) % 4;
            uint8_t oldState = motorOnToState(wasOn);
            uint8_t newState = oldState | motorOnToState(turnOn);
            uint8_t transition = makeTransition(oldState, newState);
            transitions[transition] = {"MOTOR_LEFT_ONE", expandTransitionToString(transition)};
        }
        //step right from aligned to aligned
        {
            int wasOn = (turnOn - 1 + 4) % 4;
            uint8_t oldState = motorOnToState(wasOn);
            uint8_t newState = motorOnToState(turnOn);
            uint8_t transition = makeTransition(oldState, newState);
            transitions[transition] = {"MOTOR_RIGHT_TWO", expandTransitionToString(transition)};
        }
        //step left from aligned to aligned
        {
            int wasOn = (turnOn + 1) % 4;
            uint8_t oldState = motorOnToState(wasOn);
            uint8_t newState = motorOnToState(turnOn);
            uint8_t transition = makeTransition(oldState, newState);
            transitions[transition] = {"MOTOR_LEFT_TWO", expandTransitionToString(transition)};
        }
        // no magnets energized, energize one
        {
            uint8_t oldState = 0;
            uint8_t newState = motorOnToState(turnOn);
            uint8_t transition = makeTransition(oldState, newState);
            transitions[transition] = {"MOTOR_SNAP", expandTransitionToString(transition)};
        }
    }
    for(int turnOff = 0; turnOff < 4; turnOff++) {
        { //step right from between to aligned
            int remainsOn = (turnOff + 1) % 4;
            uint8_t oldState = motorOnToState(remainsOn) | motorOnToState(turnOff);
            uint8_t newState = motorOnToState(remainsOn);
            uint8_t transition = makeTransition(oldState, newState);
            transitions[transition] = {"MOTOR_RIGHT_ONE", expandTransitionToString(transition)};
        }

        { //step left from between to aligned
            int remainsOn = (turnOff - 1 + 4) % 4;
            uint8_t oldState = motorOnToState(remainsOn) | motorOnToState(turnOff);
            uint8_t newState = motorOnToState(remainsOn);
            uint8_t transition = makeTransition(oldState, newState);
            transitions[transition] = {"MOTOR_LEFT_ONE", expandTransitionToString(transition)};
        }

        { //turn off remaining magnet
            int wasOn = (turnOff + 1) % 4;
            uint8_t oldState = motorOnToState(wasOn);
            uint8_t newState = 0;
            uint8_t transition = makeTransition(oldState, newState);
            transitions[transition] = {"MOTOR_NONE", expandTransitionToString(transition)};
        }
    }


    puts(R"(enum MotorAction
{
    MOTOR_RIGHT_ONE,    /* headPosition += 1; */
    MOTOR_RIGHT_TWO,    /* headPosition += 2; */
    MOTOR_LEFT_ONE,     /* headPosition -= 1; */
    MOTOR_LEFT_TWO,     /* headPosition -= 2; */
    MOTOR_NONE,         /* nothing */
    MOTOR_SNAP,         /* calculate which way head moves from magnet and head position */
    MOTOR_STATE_NEVER,  /* should never get this transition */
};

enum motorActions[256] =
{)");


    for(const auto& [action, reason] : transitions) {
        printf("    %-18s, // %s\n", action.c_str(), reason.c_str());
    }

    puts("};");
}

