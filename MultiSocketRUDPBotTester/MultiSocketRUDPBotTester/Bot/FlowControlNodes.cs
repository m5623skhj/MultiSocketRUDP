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
                    Log.Information($"AssertNode: Assertion passed - {ErrorMessage ?? "Unnamed assertion"}");

                    foreach (var nextNode in NextNodes)
                    {
                        nextNode.Execute(context.Client, context.GetPacket());
                    }
                }
                else
                {
                    var message = ErrorMessage ?? "Assertion failed";
                    Log.Error($"AssertNode: ASSERTION FAILED - {message}");

                    foreach (var failureNode in FailureNodes)
                    {
                        failureNode.Execute(context.Client, context.GetPacket());
                    }

                    if (!StopOnFailure)
                    {
                        return;
                    }

                    Log.Warning("AssertNode: Stopping execution due to assertion failure");
                }
            }
            catch (Exception ex)
            {
                Log.Error($"AssertNode: Exception during assertion - {ex.Message}");

                foreach (var failureNode in FailureNodes)
                {
                    failureNode.Execute(context.Client, context.GetPacket());
                }

                if (StopOnFailure)
                {
                    throw;
                }
            }
        }
    }

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
            Task.Run(async () =>
            {
                var attempt = 0;
                var success = false;

                while (attempt < MaxRetries && !success)
                {
                    attempt++;
                    Log.Information($"RetryNode: Attempt {attempt}/{MaxRetries}");

                    try
                    {
                        foreach (var node in RetryBody)
                        {
                            node.Execute(context.Client, context.GetPacket());
                        }

                        if (SuccessCondition != null)
                        {
                            success = SuccessCondition(context);
                            Log.Debug($"RetryNode: Success condition evaluated to {success}");
                        }
                        else
                        {
                            success = true;
                        }

                        if (success)
                        {
                            Log.Information($"RetryNode: Success on attempt {attempt}");

                            foreach (var successNode in SuccessNodes)
                            {
                                successNode.Execute(context.Client, context.GetPacket());
                            }
                            break;
                        }
                    }
                    catch (Exception ex)
                    {
                        Log.Warning($"RetryNode: Attempt {attempt} failed with exception: {ex.Message}");
                        success = false;
                    }

                    if (attempt >= MaxRetries || success)
                    {
                        continue;
                    }

                    var delay = UseExponentialBackoff
                        ? RetryDelayMilliseconds * (int)Math.Pow(2, attempt - 1)
                        : RetryDelayMilliseconds;

                    Log.Debug($"RetryNode: Waiting {delay}ms before next attempt");
                    await Task.Delay(delay);
                }

                if (!success)
                {
                    Log.Error($"RetryNode: Failed after {MaxRetries} attempts");
                    foreach (var failureNode in FailureNodes)
                    {
                        failureNode.Execute(context.Client, context.GetPacket());
                    }
                }

                foreach (var nextNode in NextNodes)
                {
                    nextNode.Execute(context.Client, context.GetPacket());
                }
            });
        }
    }
}