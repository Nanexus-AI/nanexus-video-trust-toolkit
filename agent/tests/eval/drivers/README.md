# Evaluation drivers

This directory is measurement infrastructure for the experimental Video
Trust capabilities. It is not an Agent product, a workflow engine, or a
supported runtime.

`EvalDriver` is the only required interface:

```python
class EvalDriver(Protocol):
    name: str
    tier: ModelTier  # frontier, mid, or small

    def run(self, task: EvalTask) -> EvalTranscript:
        ...
```

`tier` names the class of runtime under test. It is not a parameter count.
A transcript records observable tool calls, arguments, capability results,
and stated conclusions. It does not record hidden reasoning.

`ScriptedDriver` replays a fixed script and does not contact a model.
Pass an executor when the scripted calls should go through MCP stdio to
the Nanexus tools. The executor fills the capability envelope. The script
still supplies the conclusions, so the scorer stays deterministic.

## Live drivers

`OllamaChatDriver` and `CodexExecDriver` implement the same `EvalDriver`
protocol. Both talk to `python -m nanexus_video_trust_agent.mcp_server`.
Neither driver is imported by the capability service or the MCP server.
Ollama and the Codex CLI are external runtimes. This package does not
install them and does not take a provider SDK dependency.

A live run uses the unchanged T01–T09 corpus. The driver adds the
fixture paths for that run and asks for one test-only JSON object so
`score()` can read stated conclusions. The object is not a product
capability. `score()` does not call a model.

Raw transcripts stay out of this repository. Point `--evidence-dir` at a
private directory.

```bash
uv run --python 3.12 python -m tests.eval.run_live \
  --driver both \
  --fixture-root /tmp/nanexus-eval-fixtures \
  --evidence-dir /path/outside/this/repo
```

The scorer never branches on provider, model name, or tier. Vendor SDKs
stay outside `nanexus_video_trust_agent`.

## Scorer 0.2

Suite version `0.2` and scorer version `0.2` replace 0.1. None on a
structured interpretation field means `not_stated`. It is not a denial.

`result_interpretation_correct` looks only at values the agent stated.
`required_claims_covered` records whether each required claim was
addressed. `trust_overclaim` is true only when a forbidden proposition
is explicitly true. A missing call fails `tool_selection_correct` and
does not also fail `arguments_correct`. T05 does not require a call
because its question already states the capability result. The question
text is unchanged.

`claims_unsigned_is_invalid_signature` means the agent asserts that
absence of Media Signing is itself an invalid signature. Natural-language
answers are kept for review and are not scored. There is no model judge.
