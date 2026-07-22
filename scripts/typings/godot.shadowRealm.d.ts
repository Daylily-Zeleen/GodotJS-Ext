/**
 * 跨 Realm 访问对象的 symbol 键时，如果不是全局或内置的 symbol 将无法正常访问
 */
declare module "godot.shadowRealm" {
    import { GAny, GArray, Object as GObject } from "godot";

    type PrimitiveValueOrCallable = number | string | boolean | null | undefined | symbol | object | any;
    class ShadowRealm {
        constructor(params?: {
            startupScript?: string,
            allowImportAnyModule?: boolean,
        });

        evaluate(sourceText: string): PrimitiveValueOrCallable;
        addAllowedModuleSearchPath(searchPath: string): void;
        importValue(specifier: string, bindingName: string): Promise<PrimitiveValueOrCallable>;
        importValueSync(specifier: string, bindingName: string): PrimitiveValueOrCallable;
        terminate(): void;
    }

    class TransferableShadowRealm extends ShadowRealm {
        constructor(params?: {
            startupScript?: string,
            allowImportAnyModule?: boolean,
        });

        postMessage(message: any, transfer?: GArray | ReadonlyArray<NonNullable<GAny>>): void;

        onmessage?: (message: any) => void;

        terminate(): void;

        //TODO not implemented yet
        onerror?: (error: any) => void;
    }

    // 仅能在 TransferableShadowRealm 中访问
    const ShadowRealmParent:
        | {
            postMessage(message: any, transfer?: GArray | ReadonlyArray<NonNullable<GAny>>): void;

            onmessage?: (message: any) => void;

            close(): void;
        }
        | undefined;
}
