#include "Tone3000.h"

#include <juce_cryptography/juce_cryptography.h>

#ifndef KUMAMP_T3K_PUBLISHABLE_KEY
 #define KUMAMP_T3K_PUBLISHABLE_KEY ""
#endif

namespace kumamp::t3k
{
namespace
{
    constexpr const char* kOrigin = "https://www.tone3000.com";

    juce::String randomUrlSafe (int numBytes)
    {
        juce::MemoryBlock bytes ((size_t) numBytes);
        auto& random = juce::Random::getSystemRandom();
        for (size_t i = 0; i < bytes.getSize(); ++i)
            bytes[i] = (char) random.nextInt (256);
        return base64Url (bytes.getData(), bytes.getSize());
    }

    juce::String formEncode (const juce::StringPairArray& fields)
    {
        juce::StringArray parts;
        for (const auto& key : fields.getAllKeys())
            parts.add (juce::URL::addEscapeChars (key, true) + "=" + juce::URL::addEscapeChars (fields[key], true));
        return parts.joinIntoString ("&");
    }

    juce::String str (const juce::var& v, const char* key)
    {
        const auto value = v.getProperty (key, {});
        return value.isVoid() ? juce::String() : value.toString();
    }

    juce::String utf8 (const char* text) { return juce::String (juce::CharPointer_UTF8 (text)); }
} // namespace

void log (const juce::String& message)
{
    // Diagnostics for the browser flow; never contains codes or tokens.
    static juce::CriticalSection lock;
    const juce::ScopedLock sl (lock);
    const auto file = Session::libraryFolder().getChildFile ("kumamp-tone3000.log");
    file.getParentDirectory().createDirectory();
    if (file.getSize() > 256 * 1024)
        file.deleteFile();
    file.appendText (juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S  ") + message + "\n");
}

juce::String base64Url (const void* data, size_t size)
{
    return juce::Base64::toBase64 (data, size).replaceCharacter ('+', '-').replaceCharacter ('/', '_').removeCharacters ("=");
}

juce::String codeChallengeFor (const juce::String& verifier)
{
    const juce::SHA256 hash (verifier.toRawUTF8(), verifier.getNumBytesAsUTF8());
    const auto raw = hash.getRawData();
    return base64Url (raw.getData(), raw.getSize());
}

juce::StringPairArray parseQuery (const juce::String& queryOrUrl)
{
    auto query = queryOrUrl.contains ("?") ? queryOrUrl.fromFirstOccurrenceOf ("?", false, false) : queryOrUrl;
    query = query.upToFirstOccurrenceOf ("#", false, false);
    juce::StringPairArray result;
    for (const auto& pair : juce::StringArray::fromTokens (query, "&", {}))
        result.set (juce::URL::removeEscapeChars (pair.upToFirstOccurrenceOf ("=", false, false)),
                    juce::URL::removeEscapeChars (pair.fromFirstOccurrenceOf ("=", false, false).replaceCharacter ('+', ' ')));
    return result;
}

juce::String folderNameFor (const Tone& tone)
{
    auto name = tone.title.trim().isNotEmpty() ? tone.title.trim() : "Tone " + juce::String (tone.id);
    if (tone.creator.isNotEmpty())
        name << " - " << tone.creator;
    return juce::File::createLegalFileName (name).substring (0, 80).trim();
}

//==============================================================================
/** HTTP on top of juce::URL; always called from the thread pool. */
struct Session::Http
{
    struct Response
    {
        int status = 0;
        juce::MemoryBlock body;
        juce::StringPairArray headers;
        bool ok() const { return status >= 200 && status < 300; }
        juce::var json() const { return juce::JSON::parse (body.toString()); }
        juce::String errorText (const juce::String& what) const
        {
            if (status == 0)   return "No se pudo conectar con TONE3000. Revisa tu conexion a internet.";
            if (status == 401) return "La sesion de TONE3000 expiro. Vuelve a intentarlo.";
            if (status == 429) return "Demasiadas solicitudes a TONE3000. Espera un momento.";
            const auto message = str (json(), "error_description").isNotEmpty() ? str (json(), "error_description") : str (json(), "error");
            return what + " fallo (HTTP " + juce::String (status) + ")" + (message.isNotEmpty() ? ": " + message : juce::String());
        }
    };

    static Response send (juce::URL url, const juce::String& method, const juce::String& bearer,
                          const juce::String& formBody = {}, int redirectsToFollow = 5)
    {
        juce::String headers;
        if (bearer.isNotEmpty())
            headers << "Authorization: Bearer " << bearer << "\r\n";
        if (formBody.isNotEmpty())
        {
            headers << "Content-Type: application/x-www-form-urlencoded\r\n";
            url = url.withPOSTData (formBody);
        }
        headers << "Accept: application/json\r\n";

        Response response;
        auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                           .withHttpRequestCmd (method)
                           .withExtraHeaders (headers)
                           .withConnectionTimeoutMs (30000)
                           .withNumRedirectsToFollow (redirectsToFollow)
                           .withStatusCode (&response.status)
                           .withResponseHeaders (&response.headers);
        if (auto stream = url.createInputStream (options))
            stream->readIntoMemoryBlock (response.body);
        return response;
    }

    static juce::String header (const Response& r, const juce::String& name)
    {
        for (const auto& key : r.headers.getAllKeys())
            if (key.equalsIgnoreCase (name))
                return r.headers[key].upToFirstOccurrenceOf (",", false, false).trim();
        return {};
    }
};

juce::Result fetchAuthorized (const juce::URL& url, const juce::String& bearer, juce::MemoryBlock& body, int& status)
{
    // Ask with the token but follow a redirect ourselves, without it: signed storage
    // URLs on another host reject (and must not receive) the Authorization header.
    auto response = Session::Http::send (url, "GET", bearer, {}, 0);
    if (response.status >= 300 && response.status < 400)
        if (const auto location = Session::Http::header (response, "Location"); location.isNotEmpty())
        {
            log ("download: HTTP " + juce::String (response.status) + " -> " + juce::URL (location).getDomain());
            response = Session::Http::send (juce::URL (location), "GET", {}, {}, 5);
        }
    status = response.status;
    body = response.body;
    return response.ok() ? juce::Result::ok() : juce::Result::fail (response.errorText ("La descarga"));
}

//==============================================================================
/** One-shot loopback HTTP receiver for the OAuth redirect. */
class Session::Receiver final : private juce::Thread
{
public:
    explicit Receiver (std::function<void (juce::String)> callback)
        : juce::Thread ("TONE3000 redirect"), onQuery (std::move (callback)) {}

    ~Receiver() override { stop(); }

    bool start()
    {
        // Loopback only: nothing on the network can reach it. Port 0 = any free port.
        if (! listener.createListener (0, "127.0.0.1") || listener.getBoundPort() <= 0)
            return false;
        port = listener.getBoundPort();
        startThread();
        return true;
    }

    void stop()
    {
        signalThreadShouldExit();
        listener.close();   // unblocks waitForNextConnection()
        stopThread (3000);
    }

    juce::String uri() const { return "http://127.0.0.1:" + juce::String (port) + "/callback"; }

private:
    void run() override
    {
        while (! threadShouldExit())
        {
            std::unique_ptr<juce::StreamingSocket> connection (listener.waitForNextConnection());
            if (connection == nullptr || threadShouldExit())
                continue;
            if (handle (*connection))
                return;   // one redirect per flow
        }
    }

    bool handle (juce::StreamingSocket& connection)
    {
        juce::MemoryOutputStream head;
        char buffer[1024];
        while (! head.toString().contains ("\r\n\r\n") && head.getDataSize() < 16 * 1024)
        {
            if (connection.waitUntilReady (true, 2000) != 1)
                break;
            const int n = connection.read (buffer, (int) sizeof (buffer), false);
            if (n <= 0)
                break;
            head.write (buffer, (size_t) n);
        }

        const auto requestLine = head.toString().upToFirstOccurrenceOf ("\r\n", false, false);
        const auto target = requestLine.fromFirstOccurrenceOf (" ", false, false).upToFirstOccurrenceOf (" ", false, false);
        const bool isCallback = requestLine.startsWith ("GET ") && target.upToFirstOccurrenceOf ("?", false, false) == "/callback";
        log ("redirect receiver: " + requestLine.upToFirstOccurrenceOf (" ", false, false) + " "
             + target.upToFirstOccurrenceOf ("?", false, false) + " params=["
             + parseQuery (target).getAllKeys().joinIntoString (",") + "]" + (isCallback ? "" : " (ignored)"));

        juce::String response;
        if (isCallback)
        {
            const auto html = utf8 ("<!doctype html><html><head><meta charset=\"utf-8\"><title>KumAmp</title>"
                                    "<style>body{margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;"
                                    "background:#0b0d14;color:#e7e9f3;font:16px/1.5 -apple-system,Segoe UI,Helvetica,Arial,sans-serif}"
                                    "main{text-align:center;padding:32px}h1{font-size:22px;margin:0 0 8px;color:#a78bfa}"
                                    "p{margin:0;opacity:.75}</style></head><body><main><h1>Listo</h1>"
                                    "<p>Ya puedes cerrar esta pesta\xc3\xb1" "a y volver a KumAmp.</p></main></body></html>");
            response << "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: "
                     << (int) html.getNumBytesAsUTF8() << "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n" << html;
        }
        else
        {
            response << "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";   // e.g. /favicon.ico
        }
        connection.write (response.toRawUTF8(), (int) response.getNumBytesAsUTF8());
        connection.close();

        if (isCallback)
            onQuery (target.fromFirstOccurrenceOf ("?", false, false));
        return isCallback;
    }

    juce::StreamingSocket listener;
    int port = 0;
    std::function<void (juce::String)> onQuery;
};

//==============================================================================
Session::Session()
    : pool (std::make_unique<juce::ThreadPool> (2)), alive (std::make_shared<bool> (true))
{
}

Session::~Session()
{
    *alive = false;
    receiver.reset();
    pool->removeAllJobs (true, 10000);
}

const char* Session::publishableKey()
{
    return KUMAMP_T3K_PUBLISHABLE_KEY;
}

juce::File Session::libraryFolder()
{
    // Tests point this somewhere temporary instead of the user's Documents.
    if (const auto custom = juce::SystemStats::getEnvironmentVariable ("KUMAMP_TONE3000_DIR", {}); custom.isNotEmpty())
        return juce::File (custom);
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("KumAmp").getChildFile ("TONE3000");
}

void Session::cancel()
{
    receiver.reset();
    if (pendingSelect)
        finishSelect (juce::Result::fail ("cancelado"));
}

void Session::finishSelect (juce::Result result, const Tone& tone, const juce::Array<Model>& models)
{
    busy = false;
    verifier = state = {};
    if (auto done = std::exchange (pendingSelect, nullptr))
        done (result, tone, models);
}

void Session::selectTone (Completion done)
{
    startFlow ("select_tone", 0, std::move (done));
}

void Session::openTone (int toneId, Completion done)
{
    if (busy)
        cancel();

    // A live session: no browser needed. Otherwise refresh, and only then ask the browser (load_tone).
    if (accessToken.isNotEmpty() && juce::Time::getCurrentTime() < tokenExpiry)
    {
        busy = true;
        pendingSelect = std::move (done);
        fetchInBackground (toneId, accessToken, {});
        return;
    }
    if (refreshToken.isNotEmpty())
    {
        busy = true;
        pendingSelect = std::move (done);
        juce::StringPairArray form;
        form.set ("grant_type", "refresh_token");
        form.set ("refresh_token", refreshToken);
        form.set ("client_id", publishableKey());
        fetchInBackground (toneId, {}, formEncode (form), true);
        return;
    }
    startFlow ("load_tone", toneId, std::move (done));
}

void Session::startFlow (const juce::String& prompt, int toneId, Completion done)
{
    if (busy)
        cancel();
    if (juce::String (publishableKey()).isEmpty())
    {
        done (juce::Result::fail ("Esta compilacion no tiene clave de TONE3000"), {}, {});
        return;
    }

    busy = true;
    pendingSelect = std::move (done);

    // The receiver thread hands the redirect to the message thread.
    std::weak_ptr<bool> weak = alive;
    receiver = std::make_unique<Receiver> ([this, weak] (juce::String query)
    {
        juce::MessageManager::callAsync ([this, weak, query]
        {
            if (auto a = weak.lock(); a != nullptr && *a)
                handleRedirect (query);
        });
    });
    if (! receiver->start())
    {
        receiver.reset();
        finishSelect (juce::Result::fail ("No se pudo abrir un puerto local para el inicio de sesion"));
        return;
    }
    redirectUri = receiver->uri();
    log (prompt + ": listening on " + redirectUri + ", opening browser");

    verifier = randomUrlSafe (32);
    state = randomUrlSafe (16);
    auto url = juce::URL (juce::String (kOrigin) + "/api/v1/oauth/authorize")
                   .withParameter ("client_id", publishableKey())
                   .withParameter ("redirect_uri", redirectUri)
                   .withParameter ("response_type", "code")
                   .withParameter ("code_challenge", codeChallengeFor (verifier))
                   .withParameter ("code_challenge_method", "S256")
                   .withParameter ("state", state)
                   .withParameter ("prompt", prompt)
                   .withParameter ("gears", "amp_amp-cab")   // underscore-separated list
                   .withParameter ("format", "nam")
                   .withParameter ("menubar", "true");
    if (toneId > 0)
        url = url.withParameter ("tone_id", juce::String (toneId));
    if (! openBrowser || ! openBrowser (url))
    {
        receiver.reset();
        finishSelect (juce::Result::fail ("No se pudo abrir el navegador"));
    }
}

void Session::handleRedirect (const juce::String& query)
{
    // The receiver thread has ended after one redirect; free the port. Deleting
    // the receiver here is safe: this runs on the message thread, not in it.
    receiver.reset();
    if (! pendingSelect)
        return;

    const auto params = parseQuery (query);
    log ("redirect: params=[" + params.getAllKeys().joinIntoString (",") + "] tone_id=" + params["tone_id"]
         + (params["error"].isNotEmpty() ? " error=" + params["error"] : juce::String()));
    if (params["state"] != state)
        return finishSelect (juce::Result::fail ("Respuesta de TONE3000 no valida (state)"));
    if (params["error"].isNotEmpty())
        return finishSelect (juce::Result::fail ("TONE3000: " + params["error"]));
    if (params["code"].isEmpty() || params["tone_id"].isEmpty())
        return finishSelect (juce::Result::fail (params["canceled"] == "true"
                                                     ? utf8 ("Cerraste TONE3000 sin elegir un tono")
                                                     : utf8 ("TONE3000 volvi\xc3\xb3 sin un tono elegido. Toca un tono en la p\xc3\xa1gina de selecci\xc3\xb3n.")));

    juce::StringPairArray form;
    form.set ("grant_type", "authorization_code");
    form.set ("code", params["code"]);
    form.set ("code_verifier", verifier);
    form.set ("redirect_uri", redirectUri);
    form.set ("client_id", publishableKey());
    verifier = {};
    fetchInBackground (params["tone_id"].getIntValue(), {}, formEncode (form));
}

void Session::fetchInBackground (int toneId, const juce::String& token, const juce::String& tokenRequest, bool isRefresh)
{
    std::weak_ptr<bool> weak = alive;
    pool->addJob ([this, weak, toneId, token, tokenRequest, isRefresh]
    {
        juce::String error, access = token, refresh;
        int expiresIn = 0;
        Tone tone;
        juce::Array<Model> models;

        if (tokenRequest.isNotEmpty())
        {
            const auto r = Http::send (juce::URL (juce::String (kOrigin) + "/api/v1/oauth/token"), "POST", {}, tokenRequest);
            const auto json = r.json();
            access = r.ok() ? str (json, "access_token") : juce::String();
            refresh = str (json, "refresh_token");
            expiresIn = (int) json.getProperty ("expires_in", 0);
            log (juce::String (isRefresh ? "token refresh" : "token") + ": HTTP " + juce::String (r.status) + (access.isNotEmpty() ? " ok" : ""));
            if (access.isEmpty())
                error = r.ok() ? juce::String ("TONE3000 no devolvio un token") : r.errorText ("El inicio de sesion");
        }
        if (error.isEmpty())
            error = fetchToneAndModels (toneId, access, tone, models);

        juce::MessageManager::callAsync ([this, weak, error, access, refresh, expiresIn, tone, models, isRefresh, toneId]
        {
            if (auto a = weak.lock(); a == nullptr || ! *a)
                return;
            if (access.isNotEmpty())
            {
                accessToken = access;
                if (expiresIn > 0)
                    tokenExpiry = juce::Time::getCurrentTime() + juce::RelativeTime::seconds (juce::jmax (0, expiresIn - 60));
                else if (tokenExpiry < juce::Time::getCurrentTime())
                    tokenExpiry = juce::Time::getCurrentTime() + juce::RelativeTime::minutes (30);
            }
            if (refresh.isNotEmpty())
                refreshToken = refresh;

            if (isRefresh && error.isNotEmpty())
            {
                // Refresh token no longer valid: confirm this tone in the browser instead.
                refreshToken = accessToken = {};
                auto done = std::exchange (pendingSelect, nullptr);
                busy = false;
                startFlow ("load_tone", toneId, std::move (done));
                return;
            }
            finishSelect (error.isEmpty() ? juce::Result::ok() : juce::Result::fail (error), tone, models);
        });
    });
}

juce::String Session::fetchToneAndModels (int toneId, const juce::String& token, Tone& tone, juce::Array<Model>& models)
{
    const auto t = Http::send (juce::URL (juce::String (kOrigin) + "/api/v1/tones/" + juce::String (toneId)), "GET", token);
    log ("tone " + juce::String (toneId) + ": HTTP " + juce::String (t.status));
    if (! t.ok())
        return t.errorText ("Leer el tono");

    const auto v = t.json();
    tone.id = toneId;
    tone.title = str (v, "title");
    tone.creator = str (v.getProperty ("user", {}), "username");
    tone.gear = str (v, "gear");
    tone.license = str (v, "license");
    tone.url = str (v, "url");

    // Without `architecture` the API lists A1 + custom models only; A2-only tones need architecture=2.
    for (const char* architecture : { "", "2" })
    {
        auto url = juce::URL (juce::String (kOrigin) + "/api/v1/models")
                       .withParameter ("tone_id", juce::String (toneId))
                       .withParameter ("page_size", "100");
        if (*architecture != 0)
            url = url.withParameter ("architecture", architecture);
        const auto m = Http::send (url, "GET", token);
        if (! m.ok())
            return m.errorText ("Leer los modelos");
        models.addArray (parseModelList (m.body.toString()));
        log ("models (architecture=" + juce::String (*architecture != 0 ? architecture : "default") + "): HTTP "
             + juce::String (m.status) + ", " + juce::String (models.size()) + " model(s)");
        if (! models.isEmpty())
            return {};
    }
    return utf8 ("Ese tono no tiene modelos NAM descargables");
}

juce::Array<Model> parseModelList (const juce::String& replyJson)
{
    // Keep the parsed reply and the list alive while iterating: getArray() points into
    // them. (Reading it from temporaries left a dangling pointer: sometimes an empty list.)
    const auto reply = juce::JSON::parse (replyJson);
    const auto list = reply.getProperty ("data", {});
    juce::Array<Model> models;
    if (const auto* data = list.getArray())
        for (const auto& item : *data)
        {
            Model model;
            model.id = (int) item.getProperty ("id", 0);
            model.name = str (item, "name");
            model.url = str (item, "model_url");
            model.size = str (item, "size");
            if (model.url.isNotEmpty())
                models.add (model);
        }
    return models;
}

juce::String modelFileName (const Model& model)
{
    auto fileName = juce::File::createLegalFileName (model.name.trim().isNotEmpty() ? model.name.trim() : "model " + juce::String (model.id));
    if (! fileName.endsWithIgnoreCase (".nam"))
        fileName << ".nam";
    return fileName;
}

bool writeToneFolder (const juce::File& folder, const Tone& tone, const juce::Array<Model>& variants)
{
    // Creator, license and link travel with the files (TONE3000 API terms).
    juce::String info;
    info << tone.title << "\n"
         << "Creador: " << tone.creator << "\n"
         << "Licencia: " << tone.license << "\n"
         << "Fuente: " << tone.url << "\n"
         << "Descargado de TONE3000 (https://www.tone3000.com) con KumAmp.\n";

    // The tone's variant list (names only, no files) for the amp block's "Variantes" row.
    auto* root = new juce::DynamicObject();
    root->setProperty ("id", tone.id);
    root->setProperty ("title", tone.title);
    root->setProperty ("creator", tone.creator);
    root->setProperty ("gear", tone.gear);
    root->setProperty ("license", tone.license);
    root->setProperty ("url", tone.url);
    juce::Array<juce::var> list;
    for (const auto& v : variants)
    {
        auto* item = new juce::DynamicObject();
        item->setProperty ("id", v.id);
        item->setProperty ("name", v.name);
        item->setProperty ("size", v.size);
        list.add (juce::var (item));
    }
    root->setProperty ("models", list);
    return folder.createDirectory()
        && folder.getChildFile ("tone.txt").replaceWithText (info)
        && folder.getChildFile ("tone.json").replaceWithText (juce::JSON::toString (juce::var (root)));
}

bool readToneFolder (const juce::File& folder, Tone& tone, juce::Array<Model>& models)
{
    const auto json = juce::JSON::parse (folder.getChildFile ("tone.json"));
    if (! json.isObject())
        return false;
    tone.id = (int) json.getProperty ("id", 0);
    tone.title = str (json, "title");
    tone.creator = str (json, "creator");
    tone.gear = str (json, "gear");
    tone.license = str (json, "license");
    tone.url = str (json, "url");
    models.clear();
    const auto list = json.getProperty ("models", {});
    if (const auto* items = list.getArray())
        for (const auto& item : *items)
        {
            Model m;
            m.id = (int) item.getProperty ("id", 0);
            m.name = str (item, "name");
            m.size = str (item, "size");
            models.add (m);   // no URL: a fresh one (with a token) is fetched when downloading
        }
    return tone.id > 0 && ! models.isEmpty();
}

void Session::downloadModel (const Tone& tone, const juce::Array<Model>& variants, const Model& model,
                             std::function<void (juce::Result, const juce::File&)> done)
{
    const auto folder = libraryFolder().getChildFile (folderNameFor (tone));
    const auto target = folder.getChildFile (modelFileName (model));

    std::weak_ptr<bool> weak = alive;
    pool->addJob ([weak, token = accessToken, tone, variants, model, folder, target, done]
    {
        juce::String error;
        juce::MemoryBlock body;
        int status = 0;
        const auto fetched = fetchAuthorized (juce::URL (model.url), token, body, status);
        log ("download \"" + model.name + "\": HTTP " + juce::String (status) + ", " + juce::String ((juce::int64) body.getSize()) + " bytes");
        if (fetched.failed())
            error = fetched.getErrorMessage();
        else if (! body.toString().trimStart().startsWithChar ('{'))   // .nam files are JSON
            error = "La descarga no es un modelo NAM";
        else if (! folder.createDirectory() || ! target.replaceWithData (body.getData(), body.getSize()))
            error = "No se pudo guardar " + target.getFullPathName();
        else
        {
            writeToneFolder (folder, tone, variants);
        }

        juce::MessageManager::callAsync ([weak, error, target, done]
        {
            if (auto a = weak.lock(); a == nullptr || ! *a)
                return;
            done (error.isEmpty() ? juce::Result::ok() : juce::Result::fail (error), target);
        });
    });
}
} // namespace kumamp::t3k
