"use client"

import * as React from "react"
import { Search, Loader2, X, TrendingUp } from "lucide-react"
import { Input } from "@/components/ui/input"
import { Button } from "@/components/ui/button"
import { cn } from "@/lib/utils"
import { getTrending } from "@/lib/api"

interface SearchBarProps {
    onSearch: (query: string) => void
    onQueryChange?: (query: string) => void
    suggestions?: string[]
    isSearching?: boolean
    className?: string
}

export function SearchBar({ onSearch, onQueryChange, suggestions = [], isSearching, className }: SearchBarProps) {
    const [query, setQuery] = React.useState("")
    const [isOpen, setIsOpen] = React.useState(false)
    const [trending, setTrending] = React.useState<string[]>([])
    const inputRef = React.useRef<HTMLInputElement>(null)
    const dropdownRef = React.useRef<HTMLDivElement>(null)

    // Fetch trending on mount
    React.useEffect(() => {
        getTrending().then(setTrending)
    }, [])

    React.useEffect(() => {
        const handleClickOutside = (event: MouseEvent) => {
            if (dropdownRef.current && !dropdownRef.current.contains(event.target as Node) && !inputRef.current?.contains(event.target as Node)) {
                setIsOpen(false)
            }
        }
        document.addEventListener("mousedown", handleClickOutside)
        return () => document.removeEventListener("mousedown", handleClickOutside)
    }, [])

    const handleInputChange = (e: React.ChangeEvent<HTMLInputElement>) => {
        const value = e.target.value
        setQuery(value)
        if (onQueryChange) onQueryChange(value) // Call onQueryChange for live updates
        setIsOpen(true)
    }

    const handleSubmit = (e: React.FormEvent) => {
        e.preventDefault()
        onSearch(query) // onSearch is now for explicit submission
        setIsOpen(false)
    }

    const handleSuggestionClick = (suggestion: string) => {
        setQuery(suggestion)
        onSearch(suggestion) // onSearch is also for explicit selection/submission
        setIsOpen(false)
    }

    const clearSearch = () => {
        setQuery("")
        onSearch("")
        inputRef.current?.focus()
    }

    return (
        <div className={cn("relative w-full max-w-2xl mx-auto z-50", className)}>
            <form onSubmit={handleSubmit} className="relative flex items-center">
                <div className="absolute left-3 text-muted-foreground">
                    {isSearching ? (
                        <Loader2 className="h-5 w-5 animate-spin text-primary" />
                    ) : (
                        <Search className="h-5 w-5" />
                    )}
                </div>
                <Input
                    ref={inputRef}
                    value={query}
                    onChange={handleInputChange}
                    onFocus={() => setIsOpen(true)}
                    placeholder="Search documents..."
                    className="pl-10 pr-10 h-14 text-lg shadow-lg ring-offset-2 ring-primary/20 transition-all focus:ring-2 rounded-xl"
                />
                {query && (
                    <Button
                        type="button"
                        variant="ghost"
                        size="icon"
                        className="absolute right-2 h-8 w-8 text-muted-foreground hover:text-foreground"
                        onClick={clearSearch}
                    >
                        <X className="h-4 w-4" />
                    </Button>
                )}
            </form>

            {isOpen && ((query && suggestions.length > 0) || (!query && trending.length > 0)) && (
                <div
                    ref={dropdownRef}
                    className="absolute top-full left-0 right-0 mt-2 bg-popover/80 backdrop-blur-xl border border-border/50 rounded-xl shadow-xl overflow-hidden animate-accordion-down origin-top"
                >
                    {!query && trending.length > 0 && (
                        <div className="px-4 py-2 text-xs font-semibold text-muted-foreground flex items-center gap-2 bg-muted/20 border-b border-border/50">
                            <TrendingUp className="h-3 w-3" /> TRENDING SEARCHES
                        </div>
                    )}
                    <ul className="py-2">
                        {query ? suggestions.map((suggestion, index) => (
                            <li
                                key={index}
                                onClick={() => handleSuggestionClick(suggestion)}
                                className="px-4 py-3 hover:bg-muted/50 cursor-pointer flex items-center text-sm transition-colors"
                            >
                                <Search className="mr-3 h-4 w-4 text-muted-foreground" />
                                <span dangerouslySetInnerHTML={{ __html: suggestion.replace(new RegExp(`(${query.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')})`, 'gi'), '<span class="font-bold text-primary">$1</span>') }} />
                            </li>
                        )) : trending.map((item, index) => (
                            <li
                                key={`trending-${index}`}
                                onClick={() => handleSuggestionClick(item)}
                                className="px-4 py-3 hover:bg-muted/50 cursor-pointer flex items-center text-sm transition-colors"
                            >
                                <TrendingUp className="mr-3 h-4 w-4 text-primary/70" />
                                <span>{item}</span>
                            </li>
                        ))}
                    </ul>
                </div>
            )}
        </div>
    )
}
