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

## Future slots

These runtimes are not dependencies of the capability core or the MCP
server. A later driver would implement `EvalDriver`, speak MCP stdio to
`python -m nanexus_video_trust_agent.mcp_server`, and return an
`EvalTranscript`.

| Slot | Possible external runtime | Not chosen here |
| --- | --- | --- |
| `frontier` | A cloud Agent or model API with MCP tool calling | No provider SDK |
| `mid` | A smaller hosted or local tool-calling runtime | No default |
| `small` | A local runtime such as Ollama or llama.cpp, if it can call MCP tools | Neither is the product architecture |

The scorer never branches on provider, model name, or tier. Those may be
stored later as transcript metadata. Vendor SDKs stay outside
`nanexus_video_trust_agent`.
