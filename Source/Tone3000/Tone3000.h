#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <functional>
#include <memory>

namespace kumamp::t3k
{
/**
    TONE3000 "select tone" flow for the NAM amp block (API v1, free tier).

      1. A loopback receiver binds 127.0.0.1:<random port> (RFC 8252 §7.3;
         TONE3000 accepts loopback redirects without registering them).
      2. The system browser opens /api/v1/oauth/authorize with
         prompt=select_tone and PKCE (S256). The user signs in on
         tone3000.com with their own account and picks a tone there.
      3. The redirect brings back code + state + tone_id; state is verified
         and the code exchanged for an access token (kept in memory only).
      4. The tone and its models are fetched; the user picks one model and
         only that file is downloaded, into Documents/KumAmp/TONE3000/<tone>,
         with a tone.txt keeping title, creator, license and link (the API
         terms require preserving creator / license information).

    Adapted from TONE3000's official JUCE example (github.com/tone-3000/api,
    MIT). Message thread API; network and the receiver run on background
    threads and report back on the message thread.
*/
struct Tone
{
    int id = 0;
    juce::String title, creator, gear, license, url;
};

struct Model
{
    int id = 0;
    juce::String name, url, size;
};

class Session
{
public:
    Session();
    ~Session();

    static const char* publishableKey();
    static juce::File libraryFolder();

    bool isBusy() const noexcept { return busy; }

    using Completion = std::function<void (juce::Result, const Tone&, const juce::Array<Model>&)>;

    /** Opens the browser. `done` gets the picked tone and its NAM models, or an error / cancel. */
    void selectTone (Completion done);

    /** A known tone (variants): fresh model list with the current session, a refreshed
        token, or, if both are gone, the browser's load_tone confirmation. */
    void openTone (int toneId, Completion done);

    /** Downloads one model; `variants` (all models of the tone) is saved as tone.json. */
    void downloadModel (const Tone& tone, const juce::Array<Model>& variants, const Model& model,
                        std::function<void (juce::Result, const juce::File&)> done);

    /** Abort a flow waiting for the browser (frees the port). */
    void cancel();

    /** Opens the authorize URL; the system browser by default (tests replace it). */
    std::function<bool (const juce::URL&)> openBrowser = [] (const juce::URL& url) { return url.launchInDefaultBrowser(); };

private:
    class Receiver;
    struct Http;
    friend juce::Result fetchAuthorized (const juce::URL&, const juce::String&, juce::MemoryBlock&, int&);

    void finishSelect (juce::Result result, const Tone& tone = {}, const juce::Array<Model>& models = {});
    void startFlow (const juce::String& prompt, int toneId, Completion done);
    void handleRedirect (const juce::String& query);
    void fetchInBackground (int toneId, const juce::String& token, const juce::String& tokenRequest, bool isRefresh = false);
    static juce::String fetchToneAndModels (int toneId, const juce::String& token, Tone& tone, juce::Array<Model>& models);

    std::unique_ptr<Receiver> receiver;
    std::unique_ptr<juce::ThreadPool> pool;
    std::shared_ptr<bool> alive;
    Completion pendingSelect;
    juce::String verifier, state, redirectUri, accessToken, refreshToken;   // tokens: memory only
    juce::Time tokenExpiry;
    bool busy = false;
};

/** Appends a line to Documents/KumAmp/TONE3000/kumamp-tone3000.log (no codes or tokens). */
void log (const juce::String& message);

/** GET with a bearer token, following one redirect to another host without it. Worker threads only. */
juce::Result fetchAuthorized (const juce::URL& url, const juce::String& bearer, juce::MemoryBlock& body, int& status);

/** Models of a GET /api/v1/models reply ({ "data": [ ... ], "page": ..., ... }). */
juce::Array<Model> parseModelList (const juce::String& replyJson);

/** File name a model is saved under inside its tone folder. */
juce::String modelFileName (const Model& model);

/** Writes tone.txt (credits) and tone.json (the tone and all its variants, names only). */
bool writeToneFolder (const juce::File& folder, const Tone& tone, const juce::Array<Model>& variants);

/** Reads tone.json of a downloaded tone folder (the tone and all its variants). */
bool readToneFolder (const juce::File& folder, Tone& tone, juce::Array<Model>& models);

/** Pure helpers, exposed for tests. */
juce::String base64Url (const void* data, size_t size);
juce::String codeChallengeFor (const juce::String& verifier);
juce::StringPairArray parseQuery (const juce::String& queryOrUrl);
juce::String folderNameFor (const Tone& tone);
} // namespace kumamp::t3k
