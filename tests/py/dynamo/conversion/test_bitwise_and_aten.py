import torch
import torch.nn as nn
from parameterized import parameterized
from torch.testing._internal.common_utils import run_tests

from .harness import DispatchTestCase


class TestBitwiseAndConverter(DispatchTestCase):
    @parameterized.expand(
        [
            ("2d", (2, 3), (2, 3)),
            ("3d", (5, 3, 2), (5, 3, 2)),
            ("3d_broadcast", (2, 3), (2, 1, 3)),
            ("4d_broadcast_1", (2, 3), (1, 2, 1, 3)),
            ("4d_broadcast_2", (2, 3), (2, 2, 2, 3)),
        ]
    )
    def test_bitwise_and_tensor(self, _, lhs_shape, rhs_shape):
        class bitwise_and(nn.Module):
            def forward(self, lhs_val, rhs_val):
                return torch.ops.aten.bitwise_and.Tensor(lhs_val, rhs_val)

        inputs = [
            torch.randint(0, 2, lhs_shape, dtype=bool),
            torch.randint(0, 2, rhs_shape, dtype=bool),
        ]
        self.run_test(
            bitwise_and(),
            inputs,
            enable_passes=True,
            use_dynamo_tracer=True,
        )

    @parameterized.expand(
        [
            ("2d", (5, 3), True),
            ("3d", (5, 3, 2), False),
        ]
    )
    def test_bitwise_and_scalar(self, _, shape, scalar):
        class bitwise_and(nn.Module):
            def forward(self, tensor):
                return torch.ops.aten.bitwise_and.Scalar(tensor, scalar)

        inputs = [
            torch.randint(0, 2, shape, dtype=bool),
        ]
        self.run_test(
            bitwise_and(),
            inputs,
            enable_passes=True,
            use_dynamo_tracer=True,
        )

    @parameterized.expand(
        [
            ("2d", (5, 3), True),
            ("3d", (5, 3, 2), False),
        ]
    )
    def test_bitwise_and_scalar_tensor(self, _, shape, scalar):
        class bitwise_and(nn.Module):
            def forward(self, tensor):
                return torch.ops.aten.bitwise_and.Scalar_Tensor(scalar, tensor)

        inputs = [
            torch.randint(0, 2, shape, dtype=bool),
        ]
        self.run_test(
            bitwise_and(),
            inputs,
            enable_passes=True,
            use_dynamo_tracer=True,
        )


if __name__ == "__main__":
    run_tests()
