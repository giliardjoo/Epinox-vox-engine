// Checks the asynchronous generation service: worker thread, busy flag, delivery on the message thread,
// and safe destruction while a job runs.
#include <juce_events/juce_events.h>
#include <cstdio>
#include "GenerationService.h"
using namespace epinox;

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    int failures = 0;
    auto check = [&] (bool ok, const char* what) { std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what); if (! ok) ++failures; };

    GenerationService svc;
    GenerationRequest r; r.phrase = "Take me higher"; r.bpm = 124.0; r.sampleRate = 44100.0;

    bool got = false, ok = false; juce::String msg; bool onMessageThread = false;
    check (svc.start (r, [&] (GenerationResult res) { got = true; ok = res.ok && res.clip != nullptr; msg = res.message; onMessageThread = juce::MessageManager::getInstance()->isThisTheMessageThread(); }), "start accepted");
    check (svc.isBusy(), "busy right after start");
    check (! svc.start (r, [] (GenerationResult) {}), "second start is refused while busy");

    const auto t0 = juce::Time::getMillisecondCounter();
    while (! got && juce::Time::getMillisecondCounter() - t0 < 10000)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

    check (got && ok, "result delivered with a clip");
    check (onMessageThread, "callback runs on the message thread");
    check (msg.contains ("Vocal ready"), "message reports the vocal is ready");
    check (! svc.isBusy(), "not busy after the result");

    // empty letters -> failure message, not a crash
    GenerationRequest bad; bad.phrase = "123 !!"; bool got2 = false, ok2 = true;
    svc.start (bad, [&] (GenerationResult res) { got2 = true; ok2 = res.ok; });
    const auto t1 = juce::Time::getMillisecondCounter();
    while (! got2 && juce::Time::getMillisecondCounter() - t1 < 10000)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    check (got2 && ! ok2, "phrase without letters reports a failure");

    // destroying the service while a job runs must not call back into freed objects
    {
        auto* local = new GenerationService();
        bool called = false;
        GenerationRequest big; big.phrase = "this is a long phrase to keep the worker busy for a while okay"; big.style = 1; big.bpm = 60.0;
        local->start (big, [&] (GenerationResult) { called = true; });
        delete local;
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        check (! called, "no callback after the service is destroyed");
    }

    std::printf ("\n%s (%d failed)\n", failures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED", failures);
    return failures;
}
