type int64 = number;

interface String {
	getExtension(): string
	getBasename(): string
	pathJoin(path: string): string
	hasExtension(ext: string): boolean
	isAbsolutePath(): boolean
	isRelativePath(): boolean
	isResourceFile(): boolean
	getBaseDir(): string
	getFile(): string
	simplifyPath(): string
	isNetworkSharePath(): boolean

	hash(): int64
	hash64(): BigInt // uint64

	format(values: any, placeholder?: string /* = '{_}' */): string
}