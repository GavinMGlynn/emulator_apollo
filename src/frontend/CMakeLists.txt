add_subdirectory(common)
add_subdirectory(headless)

# Interactive SDL frontend is deliberately absent until it can be implemented
# properly rather than stubbed -- see docs/COMPLETION_PLAN.md. When it lands it
# links apollo_frontend_common, exactly as headless does.
