import { GAny, GArray, GDictionary, Object as GodotObject, Resource, Vector2 } from 'godot';
import type TransferScriptedNode from './transfer-scripted-node';

export type DictionaryPayload = GDictionary<{
	nested: GDictionary<{
		marker: Vector2;
		resource: Resource;
	}>;
}>;

export type FullPayload = {
	variantInJsObject: {
		nested: {
			marker: Vector2;
		};
	};
	deepMixedGraph: DeepMixedGraph;
	transferredInJsObject: {
		nested: {
			resource: Resource;
		};
	};
	transferredInDictionary: DictionaryPayload;
	map: Map<string, unknown>;
	set: Set<unknown>;
	transferBuffer: ArrayBuffer;
	bigIntValue: bigint;
	dateValue: Date;
	regExpValue: RegExp;
	typedArrayValue: Uint16Array;
	scriptedNodeWithExport: TransferScriptedNode;
	cyclicNode: CyclicNode;
	cyclicArray: CyclicArray;
};

export type CyclicNode = {
	label: string;
	self?: CyclicNode;
	child?: {
		parent?: CyclicNode;
	};
	peerTag?: string;
};

export type CyclicArray = Array<unknown>;

export type DeepMixedGraph = {
	objectWithVariantAndCollections: {
		marker: Vector2;
		typed: Uint16Array;
		nestedSet: Set<unknown>;
		nestedMap: Map<string, unknown>;
	};
	setWithComplexValues: Set<unknown>;
	mapWithComplexValues: Map<string, unknown>;
};

export enum MessageType {
	Full = 'full',
	Dictionary = 'dictionary',
	Plain = 'plain',
	PeerError = 'peerError',
	ObjectTransfer = 'object-transfer',
}

export enum TransferType {
	Godot = 'godot',
	JavaScript = 'javaScript',
}

export type FullPayloadMessage = {
	type: MessageType.Full;
	payload: FullPayload;
	transferType: TransferType;
};

export type DictionaryMessage = GDictionary<{
	type: MessageType.Dictionary;
	payload: DictionaryPayload;
}>;

export type PlainMessage = {
	type: MessageType.Plain;
	payload: {
		value: number;
		text: string;
	};
};

export type PeerErrorMessage = {
	type: MessageType.PeerError;
	message: string;
};

export type ObjectTransferAction = 'hold' | 'return' | 'control' | 'singleton';

export type ObjectTransferMessage = {
	type: MessageType.ObjectTransfer;
	action: ObjectTransferAction;
	object?: GodotObject;
	objectId?: number;
	controlId?: number;
	singletonName?: string;
};

export type Message = FullPayloadMessage | DictionaryMessage | PlainMessage | PeerErrorMessage | ObjectTransferMessage;

export function buildGodotTransferList(
	values: readonly GAny[]
): GArray {
	const arr = new GArray();

	for (const value of values) {
		arr.push_back(value);
	}

	return arr;
}
