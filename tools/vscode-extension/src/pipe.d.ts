declare module '*pipe.mjs' {
	export function request(pipeName: string, method: string, params?: Record<string, unknown>, options?: {
		signal?: AbortSignal;
		timeout?: number;
	}): Promise<unknown>;
}