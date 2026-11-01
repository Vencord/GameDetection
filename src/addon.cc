#include <napi.h>

#include <memory>

#include "matcher.h"

namespace {

class DetectWorker : public Napi::AsyncWorker {
public:
    DetectWorker(Napi::Env env, std::shared_ptr<const Database> db)
        : Napi::AsyncWorker(env), deferred(Napi::Promise::Deferred::New(env)), db(std::move(db)) {}

    Napi::Promise promise() { return deferred.Promise(); }

    void Execute() override { detections = detect(*db); }

    void OnOK() override {
        Napi::Env env = Env();
        Napi::Array result = Napi::Array::New(env, detections.size());
        for (size_t i = 0; i < detections.size(); i++) {
            const Detection& detection = detections[i];
            Napi::Object game = Napi::Object::New(env);
            game.Set("id", detection.game->id);
            game.Set("name", detection.game->name);
            game.Set("pid", detection.process.pid);
            game.Set("path", detection.process.path);
            result.Set(i, game);
        }
        deferred.Resolve(result);
    }

    void OnError(const Napi::Error& error) override { deferred.Reject(error.Value()); }

private:
    Napi::Promise::Deferred deferred;
    // Keeps the database alive even if it is replaced while the worker runs
    std::shared_ptr<const Database> db;
    std::vector<Detection> detections;
};

class Addon : public Napi::Addon<Addon> {
public:
    Addon(Napi::Env env, Napi::Object exports) {
        DefineAddon(exports, {
            InstanceMethod("setDetectableDatabase", &Addon::setDetectableDatabase),
            InstanceMethod("getRunningGames", &Addon::getRunningGames),
        });
    }

private:
    std::shared_ptr<const Database> db = std::make_shared<Database>();

    // The fields are nullable or missing for some entries
    static Napi::Array toArray(Napi::Value value) {
        return value.IsArray() ? value.As<Napi::Array>() : Napi::Array::New(value.Env());
    }

    void setDetectableDatabase(const Napi::CallbackInfo& info) {
        if (!info[0].IsArray())
            throw Napi::TypeError::New(info.Env(), "Expected an array of detectable games");

        auto next = std::make_shared<Database>();
        Napi::Array games = info[0].As<Napi::Array>();
        for (uint32_t i = 0; i < games.Length(); i++) {
            Napi::Object game = games.Get(i).ToObject();
            next->addGame(game.Get("id").ToString(), game.Get("name").ToString());

            Napi::Array skus = toArray(game.Get("third_party_skus"));
            for (uint32_t j = 0; j < skus.Length(); j++) {
                Napi::Object sku = skus.Get(j).ToObject();
                Napi::Value id = sku.Get("id");
                if (id.IsString() && sku.Get("distributor").ToString().Utf8Value() == "steam")
                    next->addSteamApp(id.As<Napi::String>().Utf8Value());
            }

            Napi::Array list = toArray(game.Get("executables"));
            for (uint32_t j = 0; j < list.Length(); j++) {
                Napi::Object executable = list.Get(j).ToObject();
                // Launchers are not the game itself, the game is listed as a separate executable
                if (executable.Get("is_launcher").ToBoolean()) continue;

                Napi::Value args = executable.Get("arguments");
                next->addExecutable(
                    executable.Get("os").ToString().Utf8Value(),
                    executable.Get("name").ToString().Utf8Value(),
                    args.IsString() ? args.As<Napi::String>().Utf8Value() : "");
            }
        }
        db = std::move(next);
    }

    Napi::Value getRunningGames(const Napi::CallbackInfo& info) {
        auto* worker = new DetectWorker(info.Env(), db);
        Napi::Promise promise = worker->promise();
        worker->Queue();
        return promise;
    }
};

}  // namespace

NODE_API_ADDON(Addon)
