import { Node, OS, PackedScene, ResourceLoader, GArray } from "godot";
import {
    getActiveAsyncTestCount,
    hasActiveAsyncTests,
    hasTestFailure,
    reportTestCompleted,
    reportTestFailure,
} from "./test-status";

const SCENE_SETTLE_DELAY_MS = 100;
const ASYNC_TEST_DRAIN_TIMEOUT_MS = 20000;
const ASYNC_TEST_DRAIN_POLL_MS = 10;

export default class Start extends Node {
    async _ready() {
        try {
            // `godot ... -- --bench` runs the benchmark scene exclusively (it
            // quits the engine itself); the benchmark scene must NOT be mixed
            // into the regular list, its quit() races the remaining loads.
            // --bench is a USER argument (after `--`, read via
            // get_cmdline_user_args) so the engine itself never sees it.
            const benchOnly = OS.get_cmdline_user_args().has("--bench");
            console.warn("START-DIAG benchOnly=" + String(benchOnly) + " args=" + JSON.stringify(OS.get_cmdline_user_args()) + " scenes=" + String(benchOnly ? 1 : 7));
            const scenes = benchOnly
                ? ["res://tests/benchmark/Benchmark.tscn"]
                : [
                      "res://tests/resource/Resource.tscn",
                      "res://tests/singleton/Singleton.tscn",
                      "res://tests/extend/Extend.tscn",
                      "res://tests/papaparse/Papaparse.tscn",
                      "res://tests/os-executor/OSExecutor.tscn",
                      "res://tests/worker/Worker.tscn",
                      "res://tests/中文路径/SourceMapTest.tscn",
                  ];

            for (const scene of scenes) {
                console.warn("START-DIAG loop scene=" + scene + " fail=" + String(hasTestFailure()));
                if (hasTestFailure()) {
                    break;
                }
                console.log("Loading scene", scene);

                const loadedScene = ResourceLoader.load(scene) as PackedScene;
                if (!(loadedScene instanceof PackedScene)) {
                    throw new Error(`failed to load PackedScene: ${scene}`);
                }
                const sceneAsNode = loadedScene.instantiate();

                this.get_tree().root?.call_deferred("add_child", sceneAsNode);
                await new Promise((resolve) => {
                    if ("completeCallback" in sceneAsNode) {
                        sceneAsNode.completeCallback = resolve;
                    }else{
                        setTimeout(resolve, SCENE_SETTLE_DELAY_MS);
                    }
                });
                this.get_tree().root?.call_deferred("remove_child", sceneAsNode);
                sceneAsNode.call_deferred("queue_free");
            }

            const asyncDrainStartedAt = Date.now();
            while (hasActiveAsyncTests() && !hasTestFailure()) {
                const elapsedMs = Date.now() - asyncDrainStartedAt;
                if (elapsedMs > ASYNC_TEST_DRAIN_TIMEOUT_MS) {
                    throw new Error(
                        `async tests did not drain before completion timeout (remaining=${String(getActiveAsyncTestCount())}, timeoutMs=${String(ASYNC_TEST_DRAIN_TIMEOUT_MS)})`,
                    );
                }
                await new Promise((resolve) => setTimeout(resolve, ASYNC_TEST_DRAIN_POLL_MS));
            }

            reportTestCompleted();
        } catch (error) {
            reportTestFailure("start", error);
        } finally {
            this.get_tree().quit();
        }
    }
}
