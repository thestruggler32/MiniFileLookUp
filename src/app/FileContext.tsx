"use client"

import { createContext, useContext, useState, ReactNode } from "react";
import type { FileMetadata } from "@/lib/api";

interface FileContextValue {
    files: FileMetadata[];
    setFiles: (files: FileMetadata[]) => void;
    isLoading: boolean;
    setIsLoading: (v: boolean) => void;
}

const FileContext = createContext<FileContextValue | undefined>(undefined);

export const FileProvider = ({ children }: { children: ReactNode }) => {
    const [files, setFiles] = useState<FileMetadata[]>([]);
    const [isLoading, setIsLoading] = useState(false);

    return (
        <FileContext.Provider value={{ files, setFiles, isLoading, setIsLoading }}>
            {children}
        </FileContext.Provider>
    );
};

export const useFiles = () => {
    const ctx = useContext(FileContext);
    if (!ctx) throw new Error("useFiles must be used within FileProvider");
    return ctx;
};
