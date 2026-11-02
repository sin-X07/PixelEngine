#pragma once

// A UI click or a held trigger must not become a shot when gameplay resumes.
class FireInputGate {
public:
    void Block() { releaseRequired_ = true; }

    bool Update(bool mouseDown, bool gameplayInputAllowed) {
        if (!gameplayInputAllowed) {
            Block();
            return false;
        }
        if (releaseRequired_) {
            if (!mouseDown) releaseRequired_ = false;
            return false;
        }
        return mouseDown;
    }

private:
    bool releaseRequired_ = false;
};
