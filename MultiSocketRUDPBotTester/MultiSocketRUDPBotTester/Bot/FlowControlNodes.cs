using MultiSocketRUDPBotTester.Buffer;
using MultiSocketRUDPBotTester.Contents.Client;
using Serilog;

namespace MultiSocketRUDPBotTester.Bot
{
    public class AssertNode : ContextNodeBase
    {
        public Func<RuntimeContext, bool>? Condition { get; set; }
        public string? ErrorMessage { get; set; }
        public bool StopOnFailure { get; set; } = true;
        public List<ActionNodeBase> FailureNodes { get; set; } = [];

        protected override void ExecuteImpl(RuntimeContext context)
        {
            if (Condition == null)
            {
                Log.Warning("AssertNode: No condition defined");
                return;
            }

            try
            {
                var result = Condition(context);

                if (result)
                {
                    Log.Information("AssertNode: Assertion passed - {Message}",
                        ErrorMessage ?? "Unnamed assertion");

                    var visited = new HashSet<ActionNodeBase>();
                    foreach (var nextNode in NextNodes)
                    {
                        NodeExecutionHelper.ExecuteChain(context, nextNode, visited);
                    }
                }
                else
                {
                    var message = ErrorMessage ?? "Assertion failed";
                    Log.Error("AssertNode: ASSERTION FAILED - {Message}", message);

                    var failVisited = new HashSet<ActionNodeBase>();
                    foreach (var failureNode in FailureNodes)
                    {
                        NodeExecutionHelper.ExecuteChain(context, failureNode, failVisited);
                    }

                    if (StopOnFailure)
                    {
                        Log.Warning("AssertNode: Stopping execution chain due to assertion failure");
                        throw new AssertionFailedException(message);
                    }

                    var nextVisited = new HashSet<ActionNodeBase>();
                    foreach (var nextNode in NextNodes)
                    {
                        NodeExecutionHelper.ExecuteChain(context, nextNode, nextVisited);
                    }
                }
            }
            catch (AssertionFailedException)
            {
                throw;
            }
            catch (Exception ex)
            {
                Log.Error("AssertNode: Exception during assertion - {Message}", ex.Message);

                var failVisited = new HashSet<ActionNodeBase>();
                foreach (var failureNode in FailureNodes)
                {
                    NodeExecutionHelper.ExecuteChain(context, failureNode, failVisited);
                }

                if (StopOnFailure)
                    throw;
            }
        }
    }

    public class AssertionFailedException(string message) : Exception(message);

    public class RetryNode : ContextNodeBase
    {
        public int MaxRetries { get; set; } = 3;
        public int RetryDelayMilliseconds { get; set; } = 1000;
        public bool UseExponentialBackoff { get; set; } = false;
        public Func<RuntimeContext, bool>? SuccessCondition { get; set; }
        public List<ActionNodeBase> RetryBody { get; set; } = [];
        public List<ActionNodeBase> SuccessNodes { get; set; } = [];
        public List<ActionNodeBase> FailureNodes { get; set; } = [];

        protected override void ExecuteImpl(RuntimeContext context)
        {
            var cancellationToken = context.Client.CancellationToken.Token;

            Task.Run(async () =>
            {
                try
                {
                    var attempt = 0;
                    var success = false;

                    while (attempt < MaxRetries && !success)
                    {
                        attempt++;
                        Log.Information("RetryNode: Attempt {Attempt}/{Max}", attempt, MaxRetries);

                        try
                        {
                            var bodyVisited = new HashSet<ActionNodeBase>();
                            foreach (var node in RetryBody)
                            {
                                NodeExecutionHelper.ExecuteChain(context, node, bodyVisited);
                            }

                            if (SuccessCondition != null)
                            {
                                success = SuccessCondition(context);
                                Log.Debug("RetryNode: Success condition evaluated to {Success}", success);
                            }
                            else
                            {
                                success = true;
                            }

                            if (success)
                            {
                                Log.Information("RetryNode: Success on attempt {Attempt}", attempt);
                                var successVisited = new HashSet<ActionNodeBase>();
                                foreach (var successNode in SuccessNodes)
                                {
                                    NodeExecutionHelper.ExecuteChain(context, successNode, successVisited);
                                }
                                break;
                            }
                        }
                        catch (Exception ex)
                        {
                            Log.Warning("RetryNode: Attempt {Attempt} failed - {Message}", attempt, ex.Message);
                            success = false;
                        }

                        if (attempt >= MaxRetries || success)
                        {
                            continue;
                        }

                        var delay = UseExponentialBackoff
                            ? RetryDelayMilliseconds * (int)Math.Pow(2, attempt - 1)
                            : RetryDelayMilliseconds;

                        Log.Debug("RetryNode: Waiting {Delay}ms before next attempt", delay);

                        try
                        {
                            await Task.Delay(delay, cancellationToken);
                        }
                        catch (OperationCanceledException)
                        {
                            Log.Information("RetryNode: Cancelled during delay");
                            return;
                        }
                    }

                    if (!success)
                    {
                        Log.Error("RetryNode: Failed after {Max} attempts", MaxRetries);
                        var failVisited = new HashSet<ActionNodeBase>();
                        foreach (var failureNode in FailureNodes)
                        {
                            NodeExecutionHelper.ExecuteChain(context, failureNode, failVisited);
                        }
                    }
                }
                catch (OperationCanceledException)
                {
                    Log.Information("RetryNode: Cancelled");
                }
                catch (Exception ex)
                {
                    Log.Error(ex, "RetryNode: Unhandled exception");
                }
            }, cancellationToken);
        }
    }
}
