import { Engine, GDictionary, instance_from_id, is_instance_id_valid, Node, Object as GodotObject, OS, PackedScene, Performance, Resource, ResourceLoader, Time, Vector2, weakref, WeakRef as GodotWeakRef } from 'godot';
import { JSWorker } from 'godot.worker';
import type { TransferableJSShadowRealm } from 'godot.shadowRealm';
import type * as ShadowRealmModule from 'godot.shadowRealm';
import {
	buildGodotTransferList,
	CyclicArray,
	CyclicNode,
	DeepMixedGraph,
	DictionaryMessage,
	DictionaryPayload,
	FullPayload,
	FullPayloadMessage,
	Message,
	ObjectTransferMessage,
	MessageType,
	PlainMessage,
	TransferType,
} from './messaging';
import { beginAsyncTest, endAsyncTest, reportTestFailure } from '../test-status';
import TransferScriptedNode from './transfer-scripted-node';

// JSC cold-start worker boot and first round-trip can
const WORKER_READY_TIMEOUT_MS = 95000;
const ROUND_TRIP_TIMEOUT_MS = 5000;

type CrossEnvironmentBackend = 'worker' | 'shadow';
type CrossEnvironmentPeer = JSWorker | TransferableJSShadowRealm;

function fail(message: string): never {
	throw new Error(`cross-environment-test: ${message}`);
}

function describeValueShape(value: unknown): string {
	const valueRecord = value as Record<string, unknown> | null;
	const constructorName =
		typeof valueRecord === 'object' && valueRecord !== null && typeof valueRecord.constructor === 'function'
			? valueRecord.constructor.name
			: typeof valueRecord === 'object' && valueRecord !== null && 'constructor' in valueRecord
				? String(valueRecord.constructor)
				: 'none';
	const tag = Object.prototype.toString.call(value);
	const keys = typeof valueRecord === 'object' && valueRecord !== null ? Object.keys(valueRecord).join(',') : '';
	return `type=${typeof value} ctor=${constructorName} tag=${tag} keys=[${keys}]`;
}

function assertVector2(value: unknown, expectedX: number, expectedY: number, label: string): asserts value is Vector2 {
	if (!(value instanceof Vector2)) {
		fail(`${label} was not a Vector2`);
	}
	if (value.x !== expectedX || value.y !== expectedY) {
		fail(`${label} mismatch (${value.x}, ${value.y}) !== (${expectedX}, ${expectedY})`);
	}
}

function assertResource(value: unknown, label: string): asserts value is Resource {
	if (value === null || value === undefined || typeof value !== 'object') {
		fail(`${label} was not a Godot object`);
	}
	const maybeGetClass = (value as { get_class?: unknown }).get_class;
	if (typeof maybeGetClass !== 'function') {
		fail(`${label} did not expose get_class()`);
	}
}

function assertDictionary(value: unknown, label: string): asserts value is GDictionary {
	if (value === null || value === undefined || typeof value !== 'object') {
		fail(`${label} was not a dictionary object`);
	}
	const maybeGetKeyed = (value as { get_keyed?: unknown }).get_keyed;
	if (typeof maybeGetKeyed !== 'function') {
		fail(`${label} did not expose get_keyed()`);
	}
}

function assertTypedArrayEquals(value: unknown, expected: readonly number[], label: string): asserts value is Uint16Array {
	if (!(value instanceof Uint16Array)) {
		fail(`${label} was not a Uint16Array`);
	}
	if (value.length !== expected.length) {
		fail(`${label} length mismatch (${value.length}) !== (${expected.length})`);
	}
	for (let i = 0; i < expected.length; i++) {
		if (value[i] !== expected[i]) {
			fail(`${label}[${String(i)}] mismatch (${value[i]}) !== (${expected[i]})`);
		}
	}
}

function assertDeepMixedGraphResponse(graph: DeepMixedGraph): void {
	assertVector2(graph.objectWithVariantAndCollections.marker, 77, 88, 'deepMixedGraph.objectWithVariantAndCollections.marker');
	assertTypedArrayEquals(graph.objectWithVariantAndCollections.typed, [100, 2, 3], 'deepMixedGraph.objectWithVariantAndCollections.typed');

	if (!(graph.objectWithVariantAndCollections.nestedSet instanceof Set)) {
		fail('deepMixedGraph.objectWithVariantAndCollections.nestedSet was not a Set');
	}
	if (!(graph.objectWithVariantAndCollections.nestedMap instanceof Map)) {
		fail('deepMixedGraph.objectWithVariantAndCollections.nestedMap was not a Map');
	}

	if (!(graph.setWithComplexValues instanceof Set)) {
		fail('deepMixedGraph.setWithComplexValues was not a Set');
	}
	if (!(graph.mapWithComplexValues instanceof Map)) {
		fail('deepMixedGraph.mapWithComplexValues was not a Map');
	}

	let hasExpectedDate = false;
	let hasExpectedRegExp = false;
	let hasCycleNode = false;
	for (const value of graph.objectWithVariantAndCollections.nestedSet) {
		if (value instanceof Date && value.toISOString() === '2024-05-06T07:08:09.000Z') {
			hasExpectedDate = true;
		}
		if (value instanceof RegExp && value.source === 'deep-peer' && value.flags === 'g') {
			hasExpectedRegExp = true;
		}
		if (typeof value === 'object' && value !== null && (value as { label?: unknown }).label === 'deep-root') {
			hasCycleNode = true;
		}
	}
	if (!hasExpectedDate) {
		fail('deepMixedGraph nested set missing expected Date');
	}
	if (!hasExpectedRegExp) {
		fail('deepMixedGraph nested set missing expected RegExp');
	}
	if (!hasCycleNode) {
		fail('deepMixedGraph nested set missing cycle node');
	}

	const deepCycleNode = graph.objectWithVariantAndCollections.nestedMap.get('cycleNode');
	if (typeof deepCycleNode !== 'object' || deepCycleNode === null) {
		fail('deepMixedGraph nested map cycleNode missing');
	}
	const cycleNodeSelf = (deepCycleNode as { self?: unknown }).self;
	if (cycleNodeSelf !== deepCycleNode) {
		fail('deepMixedGraph cycle node self reference mismatch');
	}

	const nestedTyped = graph.mapWithComplexValues.get('typed');
	assertTypedArrayEquals(nestedTyped, [200, 201], 'deepMixedGraph.mapWithComplexValues.typed');

	const nestedMirror = graph.mapWithComplexValues.get('mirror');
	if (!(nestedMirror instanceof Map)) {
		fail('deepMixedGraph.mapWithComplexValues.mirror was not a Map');
	}
	if (nestedMirror.get('set') !== graph.objectWithVariantAndCollections.nestedSet) {
		fail('deepMixedGraph mirror map set identity mismatch');
	}

	if (!graph.setWithComplexValues.has(graph.mapWithComplexValues)) {
		fail('deepMixedGraph.setWithComplexValues missing mapWithComplexValues');
	}
}

function assertFullPayloadResponse(payload: FullPayload, transferType: TransferType): void {
	assertVector2(payload.variantInJsObject.nested.marker, 3, 9, 'variantInJsObject.nested.marker');
	assertDeepMixedGraphResponse(payload.deepMixedGraph);

	const nestedJsResource = payload.transferredInJsObject.nested.resource;
	assertResource(nestedJsResource, 'transferredInJsObject.nested.resource');

	assertDictionary(payload.transferredInDictionary, 'transferredInDictionary');
	const nestedDictionary = payload.transferredInDictionary.get_keyed('nested');
	assertDictionary(nestedDictionary, 'transferredInDictionary.nested');
	const nestedDictionaryResource = nestedDictionary.get_keyed('resource');
	assertResource(nestedDictionaryResource, 'transferredInDictionary.nested.resource');
	const nestedDictionaryMarker = nestedDictionary.get_keyed('marker');
	assertVector2(nestedDictionaryMarker, 4, 6, 'transferredInDictionary.nested.marker');

	if (!(payload.map instanceof Map)) {
		fail('mapPayload was not a Map');
	}

	assertVector2(payload.map.get('vector'), 1, 2, 'mapPayload.vector');

	if (payload.map.get('number') !== 7) {
		fail('mapPayload.number mismatch');
	}

	assertVector2(payload.map.get('peerVector'), 8, 8, 'mapPayload.peerVector');

	if (!(payload.set instanceof Set)) {
		fail('setPayload was not a Set');
	}

	if (!payload.set.has('alpha')) {
		fail('setPayload did not contain alpha');
	}

	if (!payload.set.has('peerValue')) {
		fail('setPayload did not contain peerValue');
	}

	if (!(payload.transferBuffer instanceof ArrayBuffer)) {
		fail('transferBuffer was not an ArrayBuffer');
	}

	const bytes = new Uint8Array(payload.transferBuffer);

	if (bytes.length < 4) {
		fail('transferBuffer length was too small');
	}

	if (bytes[0] !== 99 || bytes[1] !== 22 || bytes[2] !== 33 || bytes[3] !== 44) {
		fail(`transferBuffer bytes mismatch: [${bytes.join(', ')}]`);
	}

	if (payload.bigIntValue !== BigInt('987654321012345678')) {
		fail(`bigIntValue mismatch: ${payload.bigIntValue.toString()}`);
	}

	if (!(payload.dateValue instanceof Date)) {
		fail('dateValue was not a Date');
	}

	if (payload.dateValue.toISOString() !== '2020-01-02T03:04:06.789Z') {
		fail(`dateValue mismatch: ${payload.dateValue.toISOString()}`);
	}

	if (!(payload.regExpValue instanceof RegExp)) {
		fail('regExpValue was not a RegExp');
	}

	if (payload.regExpValue.source !== 'peer-roundtrip-updated' || payload.regExpValue.flags !== 'gi') {
		fail(`regExpValue mismatch: /${payload.regExpValue.source}/${payload.regExpValue.flags}`);
	}

	assertTypedArrayEquals(payload.typedArrayValue, [42, 20, 30, 4000], 'typedArrayValue');

	const scriptedNode = payload.scriptedNodeWithExport;

	if (!(scriptedNode instanceof TransferScriptedNode)) {
		fail(`scriptedNodeWithExport was not a TransferScriptedNode (${describeValueShape(payload.scriptedNodeWithExport)})`);
	}

	if (scriptedNode.exportInt !== 456) {
		fail(`scriptedNodeWithExport.exportInt mismatch: ${String(scriptedNode.exportInt)}`);
	}

	if (scriptedNode.exportText !== 'peer-mutated') {
		fail(`scriptedNodeWithExport.exportText mismatch: ${String(scriptedNode.exportText)}`);
	}

	if (transferType === TransferType.Godot) {
		if (scriptedNode.get_child_count() < 1) {
			fail('scriptedNodeWithExport child was not accessible after transfer');
		}

		const roundTrippedChild = scriptedNode.get_child(0);
		if (!(roundTrippedChild instanceof Node)) {
			fail('scriptedNodeWithExport child was not a Node');
		}
		if (roundTrippedChild.get_name() !== 'implicit-child-peer') {
			fail(`scriptedNodeWithExport child name mismatch: ${roundTrippedChild.get_name()}`);
		}
	}

	const cycleNode = payload.cyclicNode;

	if (cycleNode.self !== cycleNode) {
		fail('cyclicNode.self identity mismatch');
	}

	if (cycleNode.child?.parent !== cycleNode) {
		fail('cyclicNode.child.parent identity mismatch');
	}

	if (cycleNode.peerTag !== 'seen-in-peer') {
		fail(`cyclicNode.peerTag mismatch: ${String(cycleNode.peerTag)}`);
	}

	const cycleArray = payload.cyclicArray;

	if (!Array.isArray(cycleArray)) {
		fail('cyclicArray was not an Array');
	}

	if (cycleArray[0] !== cycleNode) {
		fail('cyclicArray[0] did not reference cyclicNode');
	}

	if (cycleArray[1] !== cycleArray) {
		fail('cyclicArray self-reference mismatch');
	}

	if (cycleArray[2] !== 'peer-mark') {
		fail('cyclicArray peer marker missing');
	}
}

function assertDictionaryPayloadResponse(payload: DictionaryPayload) {
	assertDictionary(payload, 'roundTrippedPayload');

	const nestedDictionary = payload.get('nested');
	assertDictionary(nestedDictionary, 'roundTrippedPayload.nested');

	const nestedResource = nestedDictionary.get('resource');
	assertResource(nestedResource, 'roundTrippedPayload.nested.resource');

	const nestedMarker = nestedDictionary.get('marker');
	assertVector2(nestedMarker, 100, 200, 'roundTrippedPayload.nested.marker');
}

export default class TestCrossEnvironment extends Node {
	// start.ts waits for this callback instead of freeing the scene after 100ms.
	public completeCallback: (() => any) | null = null;

	async _ready() {
		beginAsyncTest();

		try {
			// Optional isolation keeps an old-runtime failure from hiding the other scenarios.
			const args = OS.get_cmdline_user_args();
			let selectedCase: string | undefined;
			let selectedBackend: CrossEnvironmentBackend | undefined;
			for (let index = 0; index < args.size(); index++) {
				const arg = args.get(index);
				if (arg.startsWith('--object-transfer-case=')) selectedCase = arg.slice('--object-transfer-case='.length);
				if (arg.startsWith('--object-transfer-backend=')) {
					const backend = arg.slice('--object-transfer-backend='.length);
					if (backend !== 'worker' && backend !== 'shadow') fail(`unknown object-transfer backend: ${backend}; expected worker or shadow`);
					selectedBackend = backend;
				}
			}
			const objectTransferCases = ['owned', 'native-owned', 'refcounted', 'persistent'];
			if (selectedCase !== undefined && !objectTransferCases.includes(selectedCase)) fail(`unknown object-transfer case: ${selectedCase}`);
			const backends: readonly CrossEnvironmentBackend[] = selectedBackend ? [selectedBackend] : ['worker', 'shadow'];
			for (const backend of backends) {
				console.log(`[cross-environment-test] ${backend}:start`);
				for (const scenario of selectedCase ? [selectedCase] : objectTransferCases) {
					console.log(`[cross-environment-test] object-transfer:${scenario}:${backend}:start`);
					if (scenario === 'persistent') await this.runPersistentObjectTransfer(backend);
					else await this.runObjectTransfer(scenario, backend);
					console.log(`[cross-environment-test] object-transfer:${scenario}:${backend}:done`);
				}
				if (!selectedCase) {
					for (let session = 1; session <= 3; session++) {
						console.log(`[cross-environment-test] ${backend}:session:${String(session)}:start`);
						await this.runSession(backend, session);
						console.log(`[cross-environment-test] ${backend}:session:${String(session)}:done`);
					}
				}
				console.log(`[cross-environment-test] ${backend}:done`);
			}
		} catch (error) {
			reportTestFailure('cross-environment-main', error);
			throw error;
		} finally {
			endAsyncTest();
			this.completeCallback?.();
		}
	}

	private async createPeer(backend: CrossEnvironmentBackend): Promise<CrossEnvironmentPeer> {
		if (backend === 'shadow') {
			const { TransferableJSShadowRealm }: typeof ShadowRealmModule = require('godot.shadowRealm');
			// The constructor installs the startup script's message handler synchronously;
			// only JSWorker needs a thread-readiness notification.
			return new TransferableJSShadowRealm({
				startupScript: 'tests/cross-environment/cross-environment-peer',
				allowImportAnyModule: true,
			});
		}
		const worker = new JSWorker('tests/cross-environment/cross-environment-peer');
		try {
			await this.waitForWorkerReady(worker);
			return worker;
		} catch (error) {
			worker.terminate();
			throw error;
		}
	}

	private objectTransferRequest(peer: CrossEnvironmentPeer, message: ObjectTransferMessage): Promise<ObjectTransferMessage> {
		let resolveMessage: (value: ObjectTransferMessage) => void = () => {};
		let rejectMessage: (reason?: unknown) => void = () => {};
		const promise = new Promise<ObjectTransferMessage>((resolve, reject) => {
			resolveMessage = resolve;
			rejectMessage = reject;
		});
		const timeout = setTimeout(() => rejectMessage(new Error(`object-transfer ${message.action} timed out`)), ROUND_TRIP_TIMEOUT_MS);
		peer.onmessage = (response: Message) => {
			clearTimeout(timeout);
			if (response instanceof GDictionary || response.type !== MessageType.ObjectTransfer || response.action !== message.action) {
				rejectMessage(new Error(`unexpected object-transfer response: ${JSON.stringify(response)}`));
				return;
			}
			resolveMessage(response);
		};
		peer.onerror = (error: unknown) => {
			clearTimeout(timeout);
			rejectMessage(error);
		};
		try {
			peer.postMessage(message, message.object ? [message.object] : undefined);
		} catch (error) {
			clearTimeout(timeout);
			rejectMessage(error);
		}
		return promise;
	}

	private async waitForObjectTransferState(predicate: () => boolean, describeFailure: () => string): Promise<void> {
		const deadline = Time.get_ticks_msec() + ROUND_TRIP_TIMEOUT_MS;
		while (!predicate()) {
			if (Time.get_ticks_msec() >= deadline) fail(describeFailure());
			await this.get_tree().process_frame.as_promise();
		}
	}

	private createNativeObject(): Node {
		const scene = ResourceLoader.load('res://tests/cross-environment/native-object.tscn');
		if (!(scene instanceof PackedScene)) fail('native object scene did not load');
		// PackedScene creates the native Node; JS new Node() would incorrectly test JS ownership.
		return scene.instantiate();
	}

	private async runObjectTransfer(scenario: string, backend: CrossEnvironmentBackend): Promise<void> {
		const peer = await this.createPeer(backend);
		let terminated = false;
		let objectId: number | undefined;
		let childId: number | undefined;
		let resourceObserver: GodotWeakRef | undefined;
		const nativeHolder = new Node();
		try {
			const control = await this.objectTransferRequest(peer, { type: MessageType.ObjectTransfer, action: 'control' });
			if (control.controlId === undefined || !is_instance_id_valid(control.controlId)) fail('missing peer teardown control');
			let object: GodotObject = scenario === 'owned' ? new Node()
				: scenario === 'native-owned' ? this.createNativeObject()
				: new Resource();
			if (scenario === 'refcounted') {
				const rawId = object.get_instance_id();
				console.log(`[cross-environment-test] object-transfer:refcounted:${backend} ObjectID=${String(rawId)} type=${typeof rawId} safeNumber=${String(Number.isSafeInteger(rawId))}; observing via native WeakRef`);
				// RefCounted ObjectIDs set bit 63. The current signed int64 -> JS
				// number conversion can round them; never use that number as an ObjectDB key.
				const observer = weakref(object);
				if (!(observer instanceof GodotWeakRef)) fail('refcounted: weakref creation failed');
				resourceObserver = observer;
				nativeHolder.set_meta('retained_resource', object);
			} else {
				objectId = object.get_instance_id();
			}
			for (let round = 0; round < 3; round++) {
				const held = await this.objectTransferRequest(peer, { type: MessageType.ObjectTransfer, action: 'hold', object, objectId });
				if (objectId !== undefined && (held.objectId !== objectId || !is_instance_id_valid(objectId))) {
					fail(`${scenario}: transfer lost native identity (sent=${String(objectId)}, received=${String(held.objectId)})`);
				}
				// Deliberately create the receiving binding BEFORE the return message.
				// RefCounted must preserve this existing binding's reference on transfer-in.
				const alreadyBound = resourceObserver ? resourceObserver.get_ref() : instance_from_id(objectId!);
				if (!alreadyBound) fail(`${scenario}: could not prebind receiver`);
				const returned = await this.objectTransferRequest(peer, { type: MessageType.ObjectTransfer, action: 'return', objectId });
				if (!returned.object || returned.object !== alreadyBound || (objectId !== undefined && returned.objectId !== objectId)) {
					fail(`${scenario}: return did not reuse existing receiver binding`);
				}
				object = returned.object;
				if (scenario === 'native-owned') {
					// Transfer only the parent; untouched native children must retain their
					// hierarchy without requiring an existing JS binding or transfer entry.
					if (!(object instanceof Node) || object.get_child_count() !== 1) fail('native-owned: transferred parent lost its child');
					const child = object.get_child(0);
					if (!(child instanceof Node) || child.get_name() !== 'UnboundNativeChild' || child.get_parent() !== object) {
						fail('native-owned: descendant hierarchy changed during transfer');
					}
					const returnedChildId = child.get_instance_id();
					if (childId !== undefined && childId !== returnedChildId) fail('native-owned: round trip replaced native child');
					childId = returnedChildId;
				}
				if (scenario === 'refcounted' && (!(object instanceof Resource) || object.get_reference_count() < 2)) {
					fail('refcounted: native holder and receiving Environment must each retain a reference');
				}
			}
			await this.objectTransferRequest(peer, { type: MessageType.ObjectTransfer, action: 'hold', object, objectId });
			// Non-RefCounted handles stay strong even without JS references, so GC
			// cannot test their deletion. Environment teardown can.
			peer.terminate();
			terminated = true;
			await this.waitForObjectTransferState(
				() => !is_instance_id_valid(control.controlId ?? 0) && (scenario !== 'owned' || !is_instance_id_valid(objectId ?? 0)),
				() => is_instance_id_valid(control.controlId ?? 0)
					? `${scenario}: peer teardown did not release untransferred control`
					: `owned: transferred Node ${String(objectId)} survived peer teardown (JS ownership lost)`
			);
			if (scenario === 'native-owned' && !is_instance_id_valid(objectId!)) fail('native-owned: peer teardown deleted externally owned object');
			if (childId !== undefined && !is_instance_id_valid(childId)) fail('native-owned: peer teardown deleted the native child');
			if (scenario === 'refcounted') {
				// Reading native metadata creates a main binding again. After the
				// holder is freed that binding must become collectible.
				let retained = nativeHolder.get_meta('retained_resource');
				if (!(retained instanceof Resource)) fail('refcounted: native holder lost Resource');
				if (retained.get_reference_count() < 2) fail('refcounted: holder reference was consumed by transfer');
				retained = null;
			}
		} finally {
			if (!terminated) peer.terminate();
			if (objectId !== undefined && scenario !== 'refcounted' && is_instance_id_valid(objectId)) {
				const managerObject = instance_from_id(objectId);
				if (managerObject instanceof Node) managerObject.queue_free();
			}
			nativeHolder.queue_free();
		}
		// queue_free defers deletion to the end of the frame; two frames guarantee the
		// delete queue has flushed before asserting child release.
		await this.get_tree().process_frame.as_promise();
		await this.get_tree().process_frame.as_promise();
		if (childId !== undefined && is_instance_id_valid(childId)) fail('native-owned: freeing parent did not release native child');
		if (resourceObserver) {
			// Leave the callback scope that returned the metadata wrapper before
			// synchronous GC. get_ref is called only AFTER collection, because a
			// successful get_ref itself creates a new wrapper and retains the Resource.
			await this.get_tree().process_frame.as_promise();
			if (!('gc' in globalThis) || typeof globalThis.gc !== 'function') {
				fail('refcounted object-transfer requires the existing global gc()');
			}
			globalThis.gc();
			await this.get_tree().process_frame.as_promise();
			if (resourceObserver.get_ref() !== null) fail('refcounted: Resource survived native holder release and GC');
		}
	}

	private async persistentCount(monitor: string): Promise<number> {
		// The existing monitor caches the MAIN Environment for 1ms, even in workers.
		// Wait for an engine frame and cache expiry, not an arbitrary sleep.
		const after = Time.get_ticks_usec() + 1000;
		do { await this.get_tree().process_frame.as_promise(); } while (Time.get_ticks_usec() < after);
		const count: unknown = Performance.get_custom_monitor(monitor);
		if (typeof count !== 'number') fail('persistent monitor did not return a number');
		return count;
	}

	private async runPersistentObjectTransfer(backend: CrossEnvironmentBackend): Promise<void> {
		const names = Performance.get_custom_monitor_names();
		let monitor: string | undefined;
		for (let index = 0; index < names.size(); index++) {
			const name = names.get(index);
			if (name.endsWith('/persistent_objects')) monitor = name;
		}
		if (!monitor) fail('persistent object-transfer requires the existing persistent_objects custom monitor');
		Engine.has_singleton('ObjectTransfer');
		const baseline = await this.persistentCount(monitor);
		// No other main-thread scenario runs during these local operations.
		const peer = await this.createPeer(backend);
		const singletonName = `ObjectTransfer${String(Time.get_ticks_usec())}`;
		const singleton = this.createNativeObject();
		// Keep this fixture a leaf: the untouched-descendant regression belongs
		// to native-owned and must not prevent observing persistent migration.
		const leafChild = singleton.get_child(0);
		if (leafChild instanceof Node) leafChild.queue_free();
		const objectId = singleton.get_instance_id();
		let current: GodotObject = singleton;
		Engine.register_singleton(singletonName, singleton);
		try {
			// Main already has a NON-persistent binding; the peer's first module
			// access marks its own binding, then transfers that history to main.
			const received = await this.objectTransferRequest(peer, { type: MessageType.ObjectTransfer, action: 'singleton', singletonName });
			if (received.objectId !== objectId || received.object !== singleton) fail('persistent: receiver binding identity changed');
			if (await this.persistentCount(monitor) !== baseline + 1) fail('persistent: receive did not register one main handle');
			// Do not access the singleton Proxy again: marking an already persistent
			// handle is not idempotent in the current implementation.
			for (let round = 0; round < 2; round++) {
				await this.objectTransferRequest(peer, { type: MessageType.ObjectTransfer, action: 'hold', object: current, objectId });
				if (await this.persistentCount(monitor) !== baseline) fail('persistent: transfer-out did not unregister main handle');
				// Exercise both a fresh receive and reuse of a NON-persistent binding.
				// Incoming flags replace receiver flags; this is not a merge contract.
				const alreadyBound = round === 1 ? instance_from_id(objectId) : null;
				if (round === 1 && !alreadyBound) fail('persistent: could not prebind receiver');
				if (await this.persistentCount(monitor) !== baseline) fail('persistent: ordinary binding was counted as persistent');
				const returned = await this.objectTransferRequest(peer, { type: MessageType.ObjectTransfer, action: 'return', objectId });
				if (!returned.object || returned.objectId !== objectId) fail('persistent: return changed native identity');
				if (alreadyBound && returned.object !== alreadyBound) fail('persistent: return replaced existing receiver wrapper');
				if (await this.persistentCount(monitor) !== baseline + 1) fail('persistent: return did not register exactly one main handle');
				current = returned.object;
			}
		} finally {
			peer.terminate();
			Engine.unregister_singleton(singletonName);
			if (is_instance_id_valid(objectId)) {
				const managerObject = instance_from_id(objectId);
				if (managerObject instanceof Node) managerObject.queue_free();
			}
		}
		if (await this.persistentCount(monitor) !== baseline) fail('persistent: native deletion did not unregister main handle');
	}

	private async runSession(backend: CrossEnvironmentBackend, session: number): Promise<void> {
		const peer = await this.createPeer(backend);
		const label = `${backend}:session:${String(session)}`;
		try {
			console.log(`[cross-environment-test] ${label}:runFullRoundTrip:godot:start`);
			await this.runFullRoundTrip(peer, TransferType.Godot);
			console.log(`[cross-environment-test] ${label}:runFullRoundTrip:godot:done`);
			console.log(`[cross-environment-test] ${label}:runFullRoundTrip:javaScript:start`);
			await this.runFullRoundTrip(peer, TransferType.JavaScript);
			console.log(`[cross-environment-test] ${label}:runFullRoundTrip:javaScript:done`);
			console.log(`[cross-environment-test] ${label}:runDictionaryRoundTrip:start`);
			await this.runDictionaryRoundTrip(peer);
			console.log(`[cross-environment-test] ${label}:runDictionaryRoundTrip:done`);
			if (session === 1) {
				console.log(`[cross-environment-test] ${label}:runPlainRoundTrip:start`);
				await this.runPlainRoundTrip(peer);
				console.log(`[cross-environment-test] ${label}:runPlainRoundTrip:done`);
			}
		} finally {
			peer.terminate();
		}
	}

	private waitForWorkerReady(worker: JSWorker): Promise<void> {
		return new Promise<void>((resolve, reject) => {
			const timeout = setTimeout(() => {
				reject(new Error(`worker ready timed out after ${String(WORKER_READY_TIMEOUT_MS)}ms`));
			}, WORKER_READY_TIMEOUT_MS);

			worker.onready = () => {
				clearTimeout(timeout);
				resolve();
			};
			worker.onerror = (error: unknown) => {
				clearTimeout(timeout);
				reject(error);
			};
		});
	}

	private runFullRoundTrip(peer: CrossEnvironmentPeer, transferType: TransferType): Promise<void> {
		const nestedJsObjectResource = ResourceLoader.load('res://tests/resource/mage.tres');
		const nestedDictionaryResource = ResourceLoader.load('res://tests/resource/warrior.tres');

		if (nestedJsObjectResource === null || nestedDictionaryResource === null) {
			fail('required test resources failed to load');
		}

		const dictionary = GDictionary.create({
			nested: GDictionary.create({
				marker: new Vector2(4, 6),
				resource: nestedDictionaryResource,
			}),
		});
		const scriptedNodeWithExport = new TransferScriptedNode();
		scriptedNodeWithExport.exportInt = 123;
		scriptedNodeWithExport.exportText = 'main-initial';
		if (transferType === TransferType.Godot) {
			const scriptedNodeChild = new Node();
			scriptedNodeChild.set_name('implicit-child');
			scriptedNodeWithExport.add_child(scriptedNodeChild);
		}

		const transferBuffer = new Uint8Array([11, 22, 33, 44]).buffer;
		if (!(transferBuffer instanceof ArrayBuffer)) {
			fail(`sender transferBuffer was not an ArrayBuffer (${describeValueShape(transferBuffer)})`);
		}
		const cyclicNode: CyclicNode = { label: 'root' };
		const cyclicChild: NonNullable<CyclicNode['child']> = {};
		cyclicNode.self = cyclicNode;
		cyclicNode.child = cyclicChild;
		cyclicChild.parent = cyclicNode;
		const cyclicArray: CyclicArray = [cyclicNode];
		cyclicArray.push(cyclicArray);

		const deepCycleNode: Record<string, unknown> = { label: 'deep-root' };
		deepCycleNode.self = deepCycleNode;
		const deepNestedSet = new Set<unknown>([
			new Date('2024-01-01T00:00:00.000Z'),
			/deep-initial/gi,
			deepCycleNode,
		]);
		const deepNestedMap = new Map<string, unknown>([
			['marker', new Vector2(7, 11)],
			['typed', new Uint16Array([1, 2, 3])],
			['set', deepNestedSet],
			['cycleNode', deepCycleNode],
		]);
		const deepMixedGraph: DeepMixedGraph = {
			objectWithVariantAndCollections: {
				marker: new Vector2(70, 80),
				typed: new Uint16Array([1, 2, 3]),
				nestedSet: deepNestedSet,
				nestedMap: deepNestedMap,
			},
			setWithComplexValues: new Set<unknown>(),
			mapWithComplexValues: new Map<string, unknown>(),
		};
		deepMixedGraph.mapWithComplexValues.set('set', deepMixedGraph.objectWithVariantAndCollections.nestedSet);
		deepMixedGraph.mapWithComplexValues.set('typed', new Uint16Array([200, 201]));
		deepMixedGraph.mapWithComplexValues.set('mirror', new Map<string, unknown>([
			['set', deepMixedGraph.objectWithVariantAndCollections.nestedSet],
			['map', deepMixedGraph.objectWithVariantAndCollections.nestedMap],
		]));
		deepMixedGraph.setWithComplexValues.add(deepMixedGraph.mapWithComplexValues);
		deepMixedGraph.setWithComplexValues.add(deepMixedGraph.objectWithVariantAndCollections.nestedSet);
		deepMixedGraph.setWithComplexValues.add(deepCycleNode);

		const message: FullPayloadMessage = {
			type: MessageType.Full,
			payload: {
				variantInJsObject: {
					nested: {
						marker: new Vector2(3, 9),
					},
				},
				deepMixedGraph,
				transferredInJsObject: {
					nested: {
						resource: nestedJsObjectResource,
					},
				},
				transferredInDictionary: dictionary,
				map: new Map<string, unknown>([
					['number', 7],
					['vector', new Vector2(1, 2)],
				]),
				set: new Set<unknown>(['alpha', 12]),
				transferBuffer,
				bigIntValue: BigInt('900719925474099312345'),
				dateValue: new Date('2020-01-02T03:04:05.678Z'),
				regExpValue: /peer-roundtrip/gi,
				typedArrayValue: new Uint16Array([10, 20, 30, 4000]),
				scriptedNodeWithExport,
				cyclicNode,
				cyclicArray,
			},
			transferType,
		};

		return new Promise<void>((resolve, reject) => {
			const timeout = setTimeout(() => {
				reject(new Error(`peer round trip timed out after ${String(ROUND_TRIP_TIMEOUT_MS)}ms (${transferType})`));
			}, ROUND_TRIP_TIMEOUT_MS);

			peer.onmessage = (message: Message) => {
				try {
					if (!message || typeof message !== 'object' || message instanceof GDictionary) {
						fail('received malformed peer response');
					}

					if (message.type === MessageType.PeerError) {
						fail(`peer reported error: ${message.message}`);
					}

						if (message.type !== MessageType.Full) {
							fail('unexpected message type');
						}

					assertFullPayloadResponse(message.payload, transferType);
					const roundTrippedScriptedNode = message.payload.scriptedNodeWithExport;
					if (roundTrippedScriptedNode instanceof Node) {
						roundTrippedScriptedNode.queue_free();
					}
					clearTimeout(timeout);
					resolve();
				} catch (error) {
					clearTimeout(timeout);
					reject(error);
				}
			};
			peer.onerror = (error: unknown) => {
				clearTimeout(timeout);
				reject(error);
			};

			if (!(message.payload.transferBuffer instanceof ArrayBuffer)) {
				fail(`dispatch transferBuffer was not an ArrayBuffer (${describeValueShape(message.payload.transferBuffer)})`);
			}

			const transfers = [nestedJsObjectResource, nestedDictionaryResource, dictionary, scriptedNodeWithExport];

			peer.postMessage(
				message,
				transferType === TransferType.Godot ? buildGodotTransferList(transfers) : transfers
			);
		});
	}

	private runDictionaryRoundTrip(peer: CrossEnvironmentPeer): Promise<void> {
		const nestedResource = ResourceLoader.load('res://tests/resource/warrior.tres');
		if (nestedResource === null) {
			fail('dictionary test resource failed to load');
		}
		const message: DictionaryMessage = GDictionary.create({
			type: MessageType.Dictionary,
			payload: {
				nested: {
					marker: new Vector2(4, 6),
					resource: nestedResource,
				},
			},
		} as const);

		return new Promise<void>((resolve, reject) => {
			const timeout = setTimeout(() => {
				reject(new Error(`peer dictionary round trip timed out after ${String(ROUND_TRIP_TIMEOUT_MS)}ms`));
			}, ROUND_TRIP_TIMEOUT_MS);

			peer.onmessage = (message: Message) => {
				try {
					if (!message || typeof message !== 'object' || !(message instanceof GDictionary)) {
						if (message && typeof message === 'object' && !(message instanceof GDictionary) && message.type === MessageType.PeerError) {
							fail(`peer reported error: ${message.message}`);
						}
						fail('received malformed peer response');
					}

					const messageType = message.get('type');

					if (messageType !== MessageType.Dictionary) {
						fail(`unexpected message type: ${messageType}`);
					}

					assertDictionaryPayloadResponse(message.get('payload'));
					clearTimeout(timeout);
					resolve();
				} catch (error) {
					clearTimeout(timeout);
					reject(error);
				}
			};
			peer.onerror = (error: unknown) => {
				clearTimeout(timeout);
				reject(error);
			};

			peer.postMessage(message, [nestedResource]);
		});
	}

	private runPlainRoundTrip(peer: CrossEnvironmentPeer): Promise<void> {
		const message: PlainMessage = {
			type: MessageType.Plain,
			payload: {
				value: 41,
				text: 'plain',
			},
		};

		return new Promise<void>((resolve, reject) => {
			const timeout = setTimeout(() => {
				reject(new Error(`peer plain round trip timed out after ${String(ROUND_TRIP_TIMEOUT_MS)}ms`));
			}, ROUND_TRIP_TIMEOUT_MS);

			peer.onmessage = (rawMessage: Message) => {
				try {
					if (!rawMessage || typeof rawMessage !== 'object' || rawMessage instanceof GDictionary) {
						fail('received malformed plain peer response');
					}

					if (rawMessage.type === MessageType.PeerError) {
						fail(`peer reported error: ${rawMessage.message}`);
					}

					if (rawMessage.type !== MessageType.Plain) {
						fail(`unexpected plain response type: ${rawMessage.type}`);
					}

					if (rawMessage.payload.value !== 42 || rawMessage.payload.text !== 'plain:peer') {
						fail(`plain response payload mismatch: ${JSON.stringify(rawMessage.payload)}`);
					}

					clearTimeout(timeout);
					resolve();
				} catch (error) {
					clearTimeout(timeout);
					reject(error);
				}
			};
			peer.onerror = (error: unknown) => {
				clearTimeout(timeout);
				reject(error);
			};

			peer.postMessage(message);
		});
	}
}
